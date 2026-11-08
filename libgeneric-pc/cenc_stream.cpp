/*
 * cenc://<n>: a DASH stream with Widevine, opened by ffmpeg in the process,
 * its samples decrypted by the CDM and handed to mpv as Matroska, so that
 * mpv decodes them like any other file, with the hardware decoder.
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/encryption_info.h>
}

#include <mpv/stream_cb.h>

#include "cenc_stream.h"
#include "hal_debug.h"
#include "widevine_hal.h"

#define hal_info(args...) _hal_info(HAL_DEBUG_PLAYBACK, NULL, args)

namespace
{

const char PROTOCOL[] = "cenc";
/* what mpv has not read yet; the pipeline waits beyond it */
const size_t BUFFER_MAX = 8 << 20;
const uint8_t WIDEVINE_SYSTEM_ID[16] = {
	0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed
};

std::string errorText(int r)
{
	char buf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(r, buf, sizeof(buf));
	return buf;
}

struct Entry
{
	cCencParams params;
	int64_t duration_ms = 0;
};

std::mutex registry_lock;
std::map<int, Entry> registry;
int next_id = 1;

int idOf(const std::string &url)
{
	size_t s = url.find("://");
	return url.compare(0, sizeof(PROTOCOL) - 1, PROTOCOL) || s == std::string::npos ? 0 : atoi(url.c_str() + s + 3);
}

void be32(std::vector<uint8_t> &v, uint32_t x)
{
	v.push_back(x >> 24);
	v.push_back(x >> 16);
	v.push_back(x >> 8);
	v.push_back(x);
}

/* the pssh box an AVEncryptionInitInfo was read from */
void appendPssh(std::vector<uint8_t> &out, const AVEncryptionInitInfo *i)
{
	bool v1 = i->num_key_ids > 0;
	be32(out, 8 + 4 + i->system_id_size + (v1 ? 4 + i->num_key_ids * i->key_id_size : 0) + 4 + i->data_size);
	out.insert(out.end(), { 'p', 's', 's', 'h' });
	be32(out, v1 ? 0x01000000 : 0);
	out.insert(out.end(), i->system_id, i->system_id + i->system_id_size);
	if (v1)
	{
		be32(out, i->num_key_ids);
		for (uint32_t k = 0; k < i->num_key_ids; k++)
			out.insert(out.end(), i->key_ids[k], i->key_ids[k] + i->key_id_size);
	}
	be32(out, i->data_size);
	out.insert(out.end(), i->data, i->data + i->data_size);
}

struct Stream
{
	cCencParams p;
	int id = 0;
	std::thread thread;
	std::mutex lock;
	std::condition_variable cond;
	std::deque<uint8_t> buffer;
	bool done = false;
	bool failed = false;
	std::atomic<bool> cancelled { false };

	void finish(bool error)
	{
		std::lock_guard<std::mutex> l(lock);
		done = true;
		failed = error;
		cond.notify_all();
	}

	static int interrupted(void *self)
	{
		return ((Stream *)self)->cancelled;
	}

	static int write(void *self, const uint8_t *data, int size)
	{
		Stream *s = (Stream *)self;
		std::unique_lock<std::mutex> l(s->lock);
		s->cond.wait(l, [s] { return s->buffer.size() < BUFFER_MAX || s->cancelled; });
		if (s->cancelled)
			return AVERROR_EXIT;
		s->buffer.insert(s->buffer.end(), data, data + size);
		s->cond.notify_all();
		return size;
	}

	std::vector<uint8_t> fetch(const std::string &url, const AVIOInterruptCB *cb)
	{
		std::vector<uint8_t> data;
		AVIOContext *io = NULL;
		AVDictionary *o = NULL;
		if (!p.user_agent.empty())
			av_dict_set(&o, "user_agent", p.user_agent.c_str(), 0);
		if (avio_open2(&io, url.c_str(), AVIO_FLAG_READ, cb, &o) >= 0)
		{
			uint8_t buf[4096];
			int n;
			while ((n = avio_read(io, buf, sizeof(buf))) > 0)
				data.insert(data.end(), buf, buf + n);
			avio_closep(&io);
		}
		av_dict_free(&o);
		return data;
	}

	static int64_t bitrate(AVStream *st)
	{
		AVDictionaryEntry *e = av_dict_get(st->metadata, "variant_bitrate", NULL, 0);
		return e ? strtoll(e->value, NULL, 10) : st->codecpar->bit_rate;
	}

	/* the tallest video up to the limit, of those the richest; the
	 * default or first audio */
	void choose(AVFormatContext *in, int &video, int &audio)
	{
		int best = 0;
		int64_t best_rate = 0;
		video = audio = -1;
		for (unsigned i = 0; i < in->nb_streams; i++)
		{
			AVStream *st = in->streams[i];
			if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				int h = st->codecpar->height;
				int64_t rate = bitrate(st);
				bool fits = h <= p.max_height, best_fits = best <= p.max_height;
				if (video < 0 || (fits && (!best_fits || h > best || (h == best && rate > best_rate))) ||
				    (!fits && !best_fits && h < best))
				{
					video = i;
					best = h;
					best_rate = rate;
				}
			}
			else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				if (audio < 0 || ((st->disposition & AV_DISPOSITION_DEFAULT) && !(in->streams[audio]->disposition & AV_DISPOSITION_DEFAULT)))
					audio = i;
			}
		}
	}

	/* the pssh boxes of video and audio, each once: the two may have keys
	 * of their own, and the license holds only what was asked for */
	std::vector<uint8_t> initData(AVFormatContext *in, int video, int audio)
	{
		std::vector<std::vector<uint8_t>> widevine, all;
		for (int i : { video, audio })
		{
			if (i < 0)
				continue;
			AVCodecParameters *par = in->streams[i]->codecpar;
			const AVPacketSideData *sd = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data,
				AV_PKT_DATA_ENCRYPTION_INIT_INFO);
			if (!sd)
				continue;
			AVEncryptionInitInfo *info = av_encryption_init_info_get_side_data(sd->data, sd->size);
			for (AVEncryptionInitInfo *e = info; e; e = e->next)
			{
				std::vector<uint8_t> box;
				appendPssh(box, e);
				bool is_widevine = e->system_id_size == sizeof(WIDEVINE_SYSTEM_ID) &&
						   !memcmp(e->system_id, WIDEVINE_SYSTEM_ID, sizeof(WIDEVINE_SYSTEM_ID));
				for (auto *list : { &all, is_widevine ? &widevine : (decltype(&all))NULL })
					if (list && std::find(list->begin(), list->end(), box) == list->end())
						list->push_back(box);
			}
			av_encryption_init_info_free(info);
		}
		std::vector<uint8_t> out;
		for (const std::vector<uint8_t> &box : widevine.empty() ? all : widevine)
			out.insert(out.end(), box.begin(), box.end());
		return out;
	}

	void run()
	{
		hal_set_threadname("hal:cenc");
		AVFormatContext *in = avformat_alloc_context();
		in->interrupt_callback = { interrupted, this };
		AVDictionary *o = NULL;
		if (!p.user_agent.empty())
			av_dict_set(&o, "user_agent", p.user_agent.c_str(), 0);
		std::string h;
		for (const std::string &x : p.headers)
			h += x + "\r\n";
		if (!h.empty())
			av_dict_set(&o, "headers", h.c_str(), 0);
		int r = avformat_open_input(&in, p.manifest.c_str(), NULL, &o);
		av_dict_free(&o);
		if (r < 0)
		{
			hal_info("cenc: %s cannot be opened: %s\n", p.manifest.c_str(), errorText(r).c_str());
			finish(true);
			return;
		}

		int video, audio;
		choose(in, video, audio);
		if (video >= 0)
			hal_info("cenc: video %dx%d, %lld bit/s\n", in->streams[video]->codecpar->width,
				 in->streams[video]->codecpar->height, (long long)bitrate(in->streams[video]));
		for (unsigned i = 0; i < in->nb_streams; i++)
			in->streams[i]->discard = ((int)i == video || (int)i == audio) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
		{
			std::lock_guard<std::mutex> l(registry_lock);
			auto e = registry.find(id);
			if (e != registry.end() && in->duration > 0)
				e->second.duration_ms = in->duration / 1000;
		}

		cWidevine wv;
		std::vector<uint8_t> init = initData(in, video, audio);
		std::string error;
		if (init.empty())
			error = "the stream names no protection system";
		else
		{
			std::vector<uint8_t> certificate;
			if (!p.certificate_url.empty())
				certificate = fetch(p.certificate_url, &in->interrupt_callback);
			wv.open(init, p.license_url, p.license_headers, certificate, error);
		}
		if (!error.empty())
		{
			hal_info("cenc: %s\n", error.c_str());
			avformat_close_input(&in);
			finish(true);
			return;
		}
		if (p.start_ms > 0)
			av_seek_frame(in, -1, p.start_ms * (AV_TIME_BASE / 1000), AVSEEK_FLAG_BACKWARD);

		AVFormatContext *out = NULL;
		avformat_alloc_output_context2(&out, NULL, "matroska", NULL);
		uint8_t *iobuf = (uint8_t *)av_malloc(1 << 16);
		out->pb = avio_alloc_context(iobuf, 1 << 16, 1, this, NULL, write, NULL);
		out->flags |= AVFMT_FLAG_CUSTOM_IO;
		std::vector<int> map(in->nb_streams, -1);
		for (int i : { video, audio })
		{
			if (i < 0)
				continue;
			AVStream *ist = in->streams[i];
			AVStream *ost = avformat_new_stream(out, NULL);
			avcodec_parameters_copy(ost->codecpar, ist->codecpar);
			ost->codecpar->codec_tag = 0;
			av_packet_side_data_remove(ost->codecpar->coded_side_data, &ost->codecpar->nb_coded_side_data,
				AV_PKT_DATA_ENCRYPTION_INIT_INFO);
			ost->time_base = ist->time_base;
			av_dict_copy(&ost->metadata, ist->metadata, 0);
			map[i] = ost->index;
		}
		AVDictionary *mo = NULL;
		av_dict_set(&mo, "live", "1", 0);
		r = avformat_write_header(out, &mo);
		av_dict_free(&mo);

		bool error_end = r < 0;
		int dropped = 0;
		AVPacket *pkt = av_packet_alloc();
		while (!error_end && !cancelled)
		{
			r = av_read_frame(in, pkt);
			if (r == AVERROR(EAGAIN))
				continue;
			if (r < 0)
			{
				error_end = r != AVERROR_EOF;
				if (error_end && !cancelled)
					hal_info("cenc: reading ends: %s\n", errorText(r).c_str());
				break;
			}
			int si = pkt->stream_index;
			cWidevine::Result d = (si < (int)map.size() && map[si] >= 0) ? wv.decrypt(pkt) : cWidevine::FAILED;
			if (d != cWidevine::DECRYPTED && d != cWidevine::CLEAR)
			{
				if (si < (int)map.size() && map[si] >= 0 && dropped++ < 5)
					hal_info("cenc: a sample of stream %d is dropped, it does not decrypt\n", si);
				av_packet_unref(pkt);
				continue;
			}
			pkt->stream_index = map[si];
			av_packet_rescale_ts(pkt, in->streams[si]->time_base, out->streams[map[si]]->time_base);
			pkt->pos = -1;
			if (av_interleaved_write_frame(out, pkt) < 0)
				error_end = !cancelled;
		}
		if (!error_end && !cancelled)
			av_write_trailer(out);
		avio_flush(out->pb);
		av_packet_free(&pkt);
		av_freep(&out->pb->buffer);
		avio_context_free(&out->pb);
		avformat_free_context(out);
		avformat_close_input(&in);
		finish(error_end);
	}
};

int64_t streamRead(void *cookie, char *buf, uint64_t nbytes)
{
	Stream *s = (Stream *)cookie;
	std::unique_lock<std::mutex> l(s->lock);
	s->cond.wait(l, [s] { return !s->buffer.empty() || s->done || s->cancelled; });
	if (!s->buffer.empty())
	{
		size_t n = std::min<size_t>(nbytes, s->buffer.size());
		std::copy(s->buffer.begin(), s->buffer.begin() + n, buf);
		s->buffer.erase(s->buffer.begin(), s->buffer.begin() + n);
		s->cond.notify_all();
		return n;
	}
	return s->failed || s->cancelled ? -1 : 0;
}

void streamCancel(void *cookie)
{
	Stream *s = (Stream *)cookie;
	s->cancelled = true;
	std::lock_guard<std::mutex> l(s->lock);
	s->cond.notify_all();
}

void streamClose(void *cookie)
{
	Stream *s = (Stream *)cookie;
	streamCancel(cookie);
	if (s->thread.joinable())
		s->thread.join();
	delete s;
}

int streamOpen(void *, char *uri, mpv_stream_cb_info *info)
{
	Stream *s = new Stream;
	s->id = idOf(uri);
	{
		std::lock_guard<std::mutex> l(registry_lock);
		auto e = registry.find(s->id);
		if (e == registry.end())
		{
			delete s;
			return MPV_ERROR_LOADING_FAILED;
		}
		s->p = e->second.params;
	}
	s->thread = std::thread(&Stream::run, s);
	info->cookie = s;
	info->read_fn = streamRead;
	info->close_fn = streamClose;
	info->cancel_fn = streamCancel;
	return 0;
}

} // namespace

void cenc::registerProtocol(mpv_handle *h)
{
	int r = mpv_stream_cb_add_ro(h, PROTOCOL, NULL, streamOpen);
	if (r < 0)
		hal_info("cenc: no protected streams, mpv_stream_cb_add_ro: %s\n", mpv_error_string(r));
}

std::string cenc::add(const cCencParams &p)
{
	std::lock_guard<std::mutex> l(registry_lock);
	int id = next_id++;
	registry[id].params = p;
	while (registry.size() > 8)
		registry.erase(registry.begin());
	return std::string(PROTOCOL) + "://" + std::to_string(id);
}

bool cenc::params(const std::string &url, cCencParams &p)
{
	std::lock_guard<std::mutex> l(registry_lock);
	auto e = registry.find(idOf(url));
	if (e == registry.end())
		return false;
	p = e->second.params;
	return true;
}

int64_t cenc::durationMs(const std::string &url)
{
	std::lock_guard<std::mutex> l(registry_lock);
	auto e = registry.find(idOf(url));
	return e == registry.end() ? 0 : e->second.duration_ms;
}
