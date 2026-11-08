/*
 * Widevine CDM host: the CDM is loaded once per process, every cWidevine
 * has an instance of its own with one temporary session. Only the decrypt
 * path is used; the frames go to the player's own decoders.
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */

#include <config.h>

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include <curl/curl.h>

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/encryption_info.h>
#include <libavutil/macros.h>
}

#include "cdm/content_decryption_module.h"
#include "hal_debug.h"
#include "widevine_hal.h"

#define hal_debug(args...) _hal_debug(HAL_DEBUG_PLAYER, NULL, args)
#define hal_info(args...) _hal_info(HAL_DEBUG_PLAYER, NULL, args)

static const char KEY_SYSTEM[] = "com.widevine.alpha";

namespace
{

struct Module
{
	void *handle = nullptr;
	void *(*create)(int, const char *, uint32_t, GetCdmHostFunc, void *) = nullptr;
	const char *(*version)() = nullptr;
	std::string error;
};

/* loaded on first use and never unloaded, the CDM keeps state of its own;
 * a CDM put there while Neutrino runs is found at the next attempt */
Module *module()
{
	static std::mutex lock;
	static Module m;
	std::lock_guard<std::mutex> guard(lock);
	if (m.create)
		return &m;
	std::string path = cWidevine::directory() + "/libwidevinecdm.so";
	if (!m.handle)
		m.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!m.handle)
	{
		const char *e = dlerror();
		m.error = e ? e : path + ": cannot be loaded";
		return &m;
	}
	auto init = (void (*)())dlsym(m.handle, "InitializeCdmModule_4");
	auto create = (void *(*)(int, const char *, uint32_t, GetCdmHostFunc, void *))dlsym(m.handle, "CreateCdmInstance");
	auto version = (const char *(*)())dlsym(m.handle, "GetCdmVersion");
	if (!init || !create || !version)
	{
		m.error = path + ": not a CDM of module version 4";
		return &m;
	}
	init();
	m.version = version;
	m.create = create;
	m.error.clear();
	hal_info("widevine: CDM %s from %s\n", m.version(), path.c_str());
	return &m;
}

class Buffer : public cdm::Buffer
{
	std::vector<uint8_t> data;
	uint32_t size = 0;
public:
	explicit Buffer(uint32_t capacity) : data(capacity) {}
	void Destroy() override { delete this; }
	uint32_t Capacity() const override { return data.size(); }
	uint8_t *Data() override { return data.data(); }
	void SetSize(uint32_t s) override { size = s; }
	uint32_t Size() const override { return size; }
};

class DecryptedBlock : public cdm::DecryptedBlock
{
	cdm::Buffer *buffer = nullptr;
	int64_t timestamp = 0;
public:
	~DecryptedBlock() { if (buffer) buffer->Destroy(); }
	void SetDecryptedBuffer(cdm::Buffer *b) override { buffer = b; }
	cdm::Buffer *DecryptedBuffer() override { return buffer; }
	void SetTimestamp(int64_t t) override { timestamp = t; }
	int64_t Timestamp() const override { return timestamp; }
};

/* the CDM asks for storage even without persistent state; it lives as
 * long as the session */
class MemoryFile : public cdm::FileIO
{
	std::map<std::string, std::vector<uint8_t>> &files;
	cdm::FileIOClient *client;
	std::string name;
public:
	MemoryFile(std::map<std::string, std::vector<uint8_t>> &f, cdm::FileIOClient *c) : files(f), client(c) {}
	void Open(const char *n, uint32_t size) override
	{
		name.assign(n, size);
		client->OnOpenComplete(cdm::FileIOClient::Status::kSuccess);
	}
	void Read() override
	{
		std::vector<uint8_t> &d = files[name];
		client->OnReadComplete(cdm::FileIOClient::Status::kSuccess, d.data(), d.size());
	}
	void Write(const uint8_t *data, uint32_t size) override
	{
		files[name].assign(data, data + size);
		client->OnWriteComplete(cdm::FileIOClient::Status::kSuccess);
	}
	void Close() override { delete this; }
};

size_t append(char *p, size_t size, size_t n, void *out)
{
	((std::string *)out)->append(p, size * n);
	return size * n;
}

/* the license request: a POST of the challenge, raw */
bool http_post(const std::string &url, const std::string &headers, const std::string &body,
	       std::string &answer, std::string &error)
{
	static std::once_flag once;
	std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
	CURL *curl = curl_easy_init();
	if (!curl)
	{
		error = "curl_easy_init failed";
		return false;
	}
	curl_slist *list = nullptr;
	bool content_type = false;
	size_t begin = 0;
	while (begin < headers.size())
	{
		size_t end = headers.find('\n', begin);
		std::string line = headers.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
		begin = end == std::string::npos ? headers.size() : end + 1;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		if (!strncasecmp(line.c_str(), "content-type:", 13))
			content_type = true;
		list = curl_slist_append(list, line.c_str());
	}
	if (!content_type)
		list = curl_slist_append(list, "Content-Type: application/octet-stream");
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	CURLcode r = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_slist_free_all(list);
	curl_easy_cleanup(curl);
	if (r != CURLE_OK)
	{
		error = std::string("license request: ") + curl_easy_strerror(r);
		return false;
	}
	if (status != 200)
	{
		error = "license request: HTTP " + std::to_string(status) + " " + answer.substr(0, 200);
		return false;
	}
	return true;
}

} // namespace

struct cWidevine::Session : public cdm::Host_10
{
	cdm::ContentDecryptionModule_10 *cdm = nullptr;
	/* every call into the CDM; its callbacks come on the calling thread */
	std::recursive_mutex cdm_lock;

	/* what the callbacks report */
	std::mutex lock;
	std::condition_variable cond;
	std::string session_id;
	std::string challenge;
	int usable = 0;
	std::string rejected;

	std::mutex timer_lock;
	std::condition_variable timer_cond;
	std::multimap<std::chrono::steady_clock::time_point, void *> timers;
	bool quit = false;
	std::thread timer_thread;

	std::map<std::string, std::vector<uint8_t>> files;

	static void *host(int version, void *self)
	{
		return version == cdm::Host_10::kVersion ? static_cast<cdm::Host_10 *>((Session *)self) : nullptr;
	}

	bool wait(const std::function<bool()> &done, int ms)
	{
		std::unique_lock<std::mutex> l(lock);
		return cond.wait_for(l, std::chrono::milliseconds(ms), [&] { return done() || !rejected.empty(); }) && rejected.empty();
	}

	void runTimers()
	{
		std::unique_lock<std::mutex> l(timer_lock);
		while (!quit)
		{
			if (timers.empty())
			{
				timer_cond.wait(l);
				continue;
			}
			auto first = timers.begin();
			if (timer_cond.wait_until(l, first->first) == std::cv_status::no_timeout)
				continue;
			first = timers.begin();
			if (first == timers.end() || first->first > std::chrono::steady_clock::now())
				continue;
			void *context = first->second;
			timers.erase(first);
			l.unlock();
			{
				std::lock_guard<std::recursive_mutex> c(cdm_lock);
				cdm->TimerExpired(context);
			}
			l.lock();
		}
	}

	~Session()
	{
		if (timer_thread.joinable())
		{
			{
				std::lock_guard<std::mutex> l(timer_lock);
				quit = true;
			}
			timer_cond.notify_all();
			timer_thread.join();
		}
		if (cdm)
		{
			std::lock_guard<std::recursive_mutex> c(cdm_lock);
			if (!session_id.empty())
				cdm->CloseSession(4, session_id.data(), session_id.size());
			cdm->Destroy();
		}
	}

	/* cdm::Host_10 */
	cdm::Buffer *Allocate(uint32_t capacity) override { return new Buffer(capacity); }
	void SetTimer(int64_t delay_ms, void *context) override
	{
		{
			std::lock_guard<std::mutex> l(timer_lock);
			timers.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms), context);
		}
		timer_cond.notify_all();
	}
	cdm::Time GetCurrentWallTime() override
	{
		timespec t;
		clock_gettime(CLOCK_REALTIME, &t);
		return t.tv_sec + t.tv_nsec / 1e9;
	}
	void OnInitialized(bool success) override
	{
		if (!success)
			hal_info("widevine: the CDM did not initialise\n");
	}
	void OnResolveKeyStatusPromise(uint32_t, cdm::KeyStatus) override {}
	void OnResolveNewSessionPromise(uint32_t, const char *id, uint32_t size) override
	{
		std::lock_guard<std::mutex> l(lock);
		session_id.assign(id, size);
		cond.notify_all();
	}
	void OnResolvePromise(uint32_t) override {}
	void OnRejectPromise(uint32_t promise, cdm::Exception e, uint32_t code, const char *msg, uint32_t size) override
	{
		std::lock_guard<std::mutex> l(lock);
		rejected = "CDM promise " + std::to_string(promise) + " rejected (" + std::to_string((unsigned)e) + "/" +
			   std::to_string(code) + "): " + std::string(msg, size);
		cond.notify_all();
	}
	void OnSessionMessage(const char *, uint32_t, cdm::MessageType type, const char *msg, uint32_t size) override
	{
		std::lock_guard<std::mutex> l(lock);
		if (type == cdm::kLicenseRequest)
			challenge.assign(msg, size);
		else
			hal_info("widevine: session message of type %u not handled\n", (unsigned)type);
		cond.notify_all();
	}
	void OnSessionKeysChange(const char *, uint32_t, bool, const cdm::KeyInformation *keys, uint32_t count) override
	{
		std::lock_guard<std::mutex> l(lock);
		usable = 0;
		for (uint32_t i = 0; i < count; i++)
		{
			if (keys[i].status == cdm::kUsable)
			{
				usable++;
				continue;
			}
			char kid[33] = "";
			for (uint32_t k = 0; k < keys[i].key_id_size && k < 16; k++)
				snprintf(kid + 2 * k, 3, "%02x", keys[i].key_id[k]);
			hal_debug("widevine: key %s is not usable (%u)\n", kid, (unsigned)keys[i].status);
		}
		hal_debug("widevine: %u keys, %d usable\n", count, usable);
		cond.notify_all();
	}

	void OnExpirationChange(const char *, uint32_t, cdm::Time) override {}
	void OnSessionClosed(const char *, uint32_t) override {}
	void SendPlatformChallenge(const char *, uint32_t, const char *, uint32_t) override
	{
		cdm::PlatformChallengeResponse none = {};
		cdm->OnPlatformChallengeResponse(none);
	}
	void EnableOutputProtection(uint32_t) override {}
	void QueryOutputProtectionStatus() override
	{
		cdm->OnQueryOutputProtectionStatus(cdm::kQuerySucceeded, 0, 0);
	}
	void OnDeferredInitializationDone(cdm::StreamType, cdm::Status) override {}
	cdm::FileIO *CreateFileIO(cdm::FileIOClient *client) override { return new MemoryFile(files, client); }
	void RequestStorageId(uint32_t version) override { cdm->OnStorageId(version, nullptr, 0); }
};

std::string cWidevine::directory()
{
	const char *dir = getenv("NEUTRINO_WIDEVINE_DIR");
	return dir && *dir ? dir : NEUTRINO_WIDEVINE_DIR;
}

bool cWidevine::probe(std::string &version, std::string &error)
{
	Module *m = module();
	if (!m->create)
	{
		error = m->error;
		return false;
	}
	version = m->version();
	Session s;
	s.cdm = (cdm::ContentDecryptionModule_10 *)m->create(cdm::ContentDecryptionModule_10::kVersion,
		KEY_SYSTEM, strlen(KEY_SYSTEM), Session::host, &s);
	if (!s.cdm)
	{
		error = "the CDM offers no instance of interface 10";
		return false;
	}
	return true;
}

cWidevine::cWidevine() : s(new Session) {}

cWidevine::~cWidevine()
{
	delete s;
}

bool cWidevine::open(const std::vector<uint8_t> &init_data, const std::string &license_url,
		     const std::string &headers, const std::vector<uint8_t> &certificate, std::string &error)
{
	Module *m = module();
	if (!m->create)
	{
		error = "no Widevine CDM: " + m->error;
		return false;
	}
	if (s->cdm)
	{
		error = "the session is open already";
		return false;
	}
	s->cdm = (cdm::ContentDecryptionModule_10 *)m->create(cdm::ContentDecryptionModule_10::kVersion,
		KEY_SYSTEM, strlen(KEY_SYSTEM), Session::host, s);
	if (!s->cdm)
	{
		error = "the CDM offers no instance of interface 10";
		return false;
	}
	s->timer_thread = std::thread(&Session::runTimers, s);
	{
		std::lock_guard<std::recursive_mutex> c(s->cdm_lock);
		s->cdm->Initialize(false, false, false);
		if (!certificate.empty())
			s->cdm->SetServerCertificate(1, certificate.data(), certificate.size());
		s->cdm->CreateSessionAndGenerateRequest(2, cdm::kTemporary, cdm::kCenc, init_data.data(), init_data.size());
	}
	if (!s->wait([this] { return !s->challenge.empty() && !s->session_id.empty(); }, 5000))
	{
		error = s->rejected.empty() ? "the CDM made no license request" : s->rejected;
		return false;
	}
	std::string license;
	if (!http_post(license_url, headers, s->challenge, license, error))
		return false;
	{
		std::lock_guard<std::recursive_mutex> c(s->cdm_lock);
		s->cdm->UpdateSession(3, s->session_id.data(), s->session_id.size(), (const uint8_t *)license.data(), license.size());
	}
	if (!s->wait([this] { return s->usable > 0; }, 5000))
	{
		error = s->rejected.empty() ? "the license holds no usable key" : s->rejected;
		return false;
	}
	hal_info("widevine: session %s, %d usable keys\n", s->session_id.c_str(), s->usable);
	return true;
}

cWidevine::Result cWidevine::decrypt(AVPacket *pkt)
{
	size_t size = 0;
	uint8_t *side = av_packet_get_side_data(pkt, AV_PKT_DATA_ENCRYPTION_INFO, &size);
	if (!side)
		return CLEAR;
	if (!s->cdm)
		return NO_KEY;
	AVEncryptionInfo *info = av_encryption_info_get_side_data(side, size);
	if (!info)
		return FAILED;

	/* the encrypted parts of the sample, all of it without subsamples */
	struct Part
	{
		uint32_t offset;
		uint32_t size;
	};
	std::vector<Part> parts;
	uint64_t end = 0;
	for (uint32_t i = 0; i < info->subsample_count; i++)
	{
		end += info->subsamples[i].bytes_of_clear_data;
		if (info->subsamples[i].bytes_of_protected_data)
			parts.push_back({ (uint32_t)end, info->subsamples[i].bytes_of_protected_data });
		end += info->subsamples[i].bytes_of_protected_data;
	}
	if (!info->subsample_count)
	{
		parts.push_back({ 0, (uint32_t)pkt->size });
		end = pkt->size;
	}

	Result result = DECRYPTED;
	if (end > (uint64_t)pkt->size || av_packet_make_writable(pkt) < 0)
		result = FAILED;
	else if (parts.empty())
		result = CLEAR;
	else
	{
		/* the CDM decrypts only a single subsample without clear bytes:
		 * the encrypted parts of a cenc sample go to it as one block,
		 * those of a cbcs sample one by one, as inputstream.adaptive
		 * does it */
		bool cbcs = info->scheme == MKBETAG('c', 'b', 'c', 's');
		uint8_t iv[16] = { 0 };
		memcpy(iv, info->iv, info->iv_size < sizeof(iv) ? info->iv_size : sizeof(iv));
		for (size_t first = 0; first < parts.size() && result == DECRYPTED;)
		{
			size_t last = cbcs ? first + 1 : parts.size();
			std::vector<uint8_t> block;
			for (size_t i = first; i < last; i++)
				block.insert(block.end(), pkt->data + parts[i].offset, pkt->data + parts[i].offset + parts[i].size);

			cdm::SubsampleEntry whole = { 0, (uint32_t)block.size() };
			cdm::InputBuffer_2 in = {};
			in.data = block.data();
			in.data_size = block.size();
			in.encryption_scheme = cbcs ? cdm::EncryptionScheme::kCbcs : cdm::EncryptionScheme::kCenc;
			in.key_id = info->key_id;
			in.key_id_size = info->key_id_size;
			in.iv = iv;
			in.iv_size = sizeof(iv);
			in.subsamples = &whole;
			in.num_subsamples = 1;
			in.pattern = { info->crypt_byte_block, info->skip_byte_block };
			in.timestamp = pkt->pts;

			DecryptedBlock out;
			cdm::Status status;
			{
				std::lock_guard<std::recursive_mutex> c(s->cdm_lock);
				status = s->cdm->Decrypt(in, &out);
			}
			if (status != cdm::kSuccess || !out.DecryptedBuffer() || out.DecryptedBuffer()->Size() != block.size())
			{
				static int reported;
				if (reported++ < 5)
					hal_info("widevine: decrypt of %d bytes failed, status %u\n", pkt->size, (unsigned)status);
				result = status == cdm::kNoKey ? NO_KEY : FAILED;
				break;
			}
			const uint8_t *clear = out.DecryptedBuffer()->Data();
			for (size_t i = first; i < last; i++)
			{
				memcpy(pkt->data + parts[i].offset, clear, parts[i].size);
				clear += parts[i].size;
			}
			first = last;
		}
	}
	av_encryption_info_free(info);
	if (result == DECRYPTED || result == CLEAR)
		av_packet_side_data_remove(pkt->side_data, &pkt->side_data_elems, AV_PKT_DATA_ENCRYPTION_INFO);
	return result;
}
