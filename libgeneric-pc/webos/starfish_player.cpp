/*
 * Playback through the media pipeline of an LG webOS TV
 *
 * Copyright (C) 2026 Markus Volk
 *
 * The way the pipeline is fed follows Kodi's MediaPipelineWebOS
 * (xbmc/cores/VideoPlayer/MediaPipelineWebOS.cpp, GPL-2.0-or-later):
 * one StarfishMediaAPIs instance for audio and video, loaded as a
 * BUFFERSTREAM with the exported window as its target, fed with
 * elementary stream packets and their pts in nanoseconds.
 *
 * License: GPL-2.0-or-later
 */
#include "config.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/mathematics.h>
}

#include "starfish_player.h"
#include "starfish_api.h"
#include "glfb_webos.h"
#include "glfb_priv.h"
#include "hal_debug.h"

#define hal_debug(args...) _hal_debug(HAL_DEBUG_PLAYBACK, this, args)
#define hal_info(args...) _hal_info(HAL_DEBUG_PLAYBACK, this, args)

extern GLFbPC *glfb_priv;

using namespace std::chrono_literals;

static const AVRational NS = { 1, 1000000000 };
/* what the pipeline is told to buffer, as in Kodi */
static const unsigned VIDEO_SRC_BUFFER = 8 << 20;
static const unsigned AUDIO_SRC_BUFFER = 2 << 20;
/* what waits here in front of it */
static const size_t VIDEO_QUEUE_MAX = 16 << 20;
static const size_t AUDIO_QUEUE_MAX = 2 << 20;
static const int PREROLL_MAX_PACKETS = 5000;
static const int IO_BUFFER = 64 * 1024;
/* a growing recording that has not grown for this long has ended */
static const int GROW_TIMEOUT_MS = 10000;
/* how far feeding may run ahead of the picture on screen, as in Kodi */
static const int64_t MAX_FEED_AHEAD = 1600000000LL;
/* a timestamp further from the one before is a jump, not a gap */
static const int64_t JUMP_BACK = 500000000LL;
static const int64_t JUMP_AHEAD = 5000000000LL;
/* what a jump is closed with: about one frame */
static const int64_t JUMP_STEP = 40000000LL;

static std::string averr(int e)
{
	char b[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(e, b, sizeof(b));
	return b;
}

cStarfishPlayer::cStarfishPlayer()
	: mAbort(false), mStop(false), mLoaded(false), mEof(false),
	  mFramePts(AV_NOPTS_VALUE), mDemuxPts(AV_NOPTS_VALUE), mBytes(0),
	  mStartPts(0), mFileStart(0), mTimeSet(false), mSeekReq(false), mSeekTarget(0), mFlushing(false), mWaitKey(false), mTsOffset(0), mTsPrevOffset(0),
	  mFd(-1), mIo(NULL), mFmt(NULL), mAudioFmt(NULL), mVideoIdx(-1), mAudioIdx(-1), mAudioSeekReq(false),
	  mAdts(false), mApi(NULL), mLoadDone(false), mUnloadDone(false)
{
}

cStarfishPlayer::~cStarfishPlayer()
{
	stop();
}

void cStarfishPlayer::reset()
{
	mAbort = false;
}

void cStarfishPlayer::abort()
{
	mAbort = true;
	mEventCond.notify_all();
	mQueueCond.notify_all();
}

int cStarfishPlayer::interruptCb(void *opaque)
{
	cStarfishPlayer *p = static_cast<cStarfishPlayer *>(opaque);
	return p->mAbort || p->mStop;
}

/* a recording being written: at its end, wait for more */
int cStarfishPlayer::ioRead(void *opaque, uint8_t *buf, int size)
{
	cStarfishPlayer *p = static_cast<cStarfishPlayer *>(opaque);
	for (int idle = 0;; idle += 100)
	{
		ssize_t n = read(p->mFd, buf, size);
		if (n > 0)
			return n;
		if (n < 0 && errno != EINTR)
			return AVERROR(errno);
		if (p->mAbort || p->mStop)
			return AVERROR_EXIT;
		if (p->mSeekReq)
			return AVERROR(EAGAIN);
		if (idle >= GROW_TIMEOUT_MS)
			return AVERROR_EOF;
		std::this_thread::sleep_for(100ms);
	}
}

int64_t cStarfishPlayer::ioSeek(void *opaque, int64_t offset, int whence)
{
	cStarfishPlayer *p = static_cast<cStarfishPlayer *>(opaque);
	if (whence == AVSEEK_SIZE)
	{
		struct stat st;
		return fstat(p->mFd, &st) ? AVERROR(errno) : st.st_size;
	}
	off_t r = lseek(p->mFd, offset, whence & ~AVSEEK_FORCE);
	return r < 0 ? AVERROR(errno) : r;
}

int64_t cStarfishPlayer::packetPts(const AVPacket *pkt) const
{
	int64_t t = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
	if (t == AV_NOPTS_VALUE)
		return AV_NOPTS_VALUE;
	return av_rescale_q(t, pkt->time_base, NS);
}

AVStream *cStarfishPlayer::audioStream() const
{
	if (mAudioIdx < 0)
		return NULL;
	return (mAudioFmt ? mAudioFmt : mFmt)->streams[mAudioIdx];
}

/* streams with advertising spliced in, as the free channels have, start
 * the time anew where an advert begins or ends; the pipeline would wait
 * for a picture that never comes that far, or drop every frame as late.
 * The time runs on instead, across the jump. Audio and video do not
 * cross it in the same packet: what still comes from before keeps the
 * offset of before. */
bool cStarfishPlayer::isJump(int64_t ns, int es) const
{
	int64_t last = mLastDts[es];
	return last != AV_NOPTS_VALUE && (ns < last - JUMP_BACK || ns > last + JUMP_AHEAD);
}

void cStarfishPlayer::smoothJumps(AVPacket *pkt, int es)
{
	int64_t t = pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
	if (t == AV_NOPTS_VALUE)
		return;
	AVRational tb = pkt->time_base;
	int64_t raw = av_rescale_q(t, tb, NS);
	int64_t offset = mTsOffset;
	if (isJump(raw + offset, es))
	{
		if (!isJump(raw + mTsPrevOffset, es))
			offset = mTsPrevOffset;
		else
		{
			mTsPrevOffset = mTsOffset;
			mTsOffset = mLastDts[es] + JUMP_STEP - raw;
			offset = mTsOffset;
			hal_info("%s: time jumps by %" PRId64 " ms, runs on\n", __func__, (mTsPrevOffset - mTsOffset) / 1000000);
		}
	}
	mLastDts[es] = raw + offset;
	if (offset)
	{
		int64_t off = av_rescale_q(offset, NS, tb);
		if (pkt->pts != AV_NOPTS_VALUE)
			pkt->pts += off;
		if (pkt->dts != AV_NOPTS_VALUE)
			pkt->dts += off;
	}
}

/* HLS offers every variant as a stream of its own, with its bandwidth;
 * the TV decodes the best one there is. -1 without variants. */
int cStarfishPlayer::bestVideo()
{
	int best = -1;
	int64_t bestRate = -1;
	for (unsigned i = 0; i < mFmt->nb_streams; i++)
	{
		AVStream *st = mFmt->streams[i];
		if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO || (st->disposition & AV_DISPOSITION_ATTACHED_PIC))
			continue;
		AVDictionaryEntry *e = av_dict_get(st->metadata, "variant_bitrate", NULL, 0);
		if (!e)
			continue;
		int64_t rate = strtoll(e->value, NULL, 10);
		if (st->codecpar->height > 2160)
			continue;
		if (rate > bestRate)
		{
			bestRate = rate;
			best = i;
		}
	}
	if (best >= 0)
	{
		hal_info("%s: variant of %" PRId64 " bit/s, %dx%d\n", __func__, bestRate,
			 mFmt->streams[best]->codecpar->width, mFmt->streams[best]->codecpar->height);
		return best;
	}
	return -1;
}

/* what the TV decodes itself; everything else goes on through mpv */
bool cStarfishPlayer::supported(std::string &videoCodec, std::string &audioCodec)
{
	if (mVideoIdx < 0)
	{
		hal_info("%s: no video\n", __func__);
		return false;
	}
	AVCodecParameters *v = mFmt->streams[mVideoIdx]->codecpar;
	switch (v->codec_id)
	{
		case AV_CODEC_ID_H264:
			videoCodec = "H264";
			break;
		case AV_CODEC_ID_HEVC:
			videoCodec = "H265";
			break;
		default:
			hal_info("%s: video %s\n", __func__, avcodec_get_name(v->codec_id));
			return false;
	}
	/* avcC/hvcC as in mp4 or mkv would have to become annex B first */
	if (v->extradata_size > 0 && v->extradata[0] == 1)
	{
		hal_info("%s: %s is not annex B\n", __func__, videoCodec.c_str());
		return false;
	}
	if (v->width <= 0 || v->height <= 0)
	{
		hal_info("%s: video size unknown\n", __func__);
		return false;
	}
	if (mAudioIdx < 0)
		return true;
	switch (audioStream()->codecpar->codec_id)
	{
		case AV_CODEC_ID_AAC:
			audioCodec = "AAC";
			break;
		case AV_CODEC_ID_AC3:
			audioCodec = "AC3";
			break;
		case AV_CODEC_ID_EAC3:
			audioCodec = "AC3 PLUS";
			break;
		case AV_CODEC_ID_MP3:
			audioCodec = "MP3";
			break;
		default:
			hal_info("%s: audio %s\n", __func__, avcodec_get_name(audioStream()->codecpar->codec_id));
			return false;
	}
	return true;
}

/* read up to the first video keyframe, which is where decoding starts;
 * for AAC also up to the first audio packet, which tells ADTS from raw */
bool cStarfishPlayer::preroll(int &error)
{
	AVPacket *pkt = av_packet_alloc();
	bool key = false;
	bool aac = mAudioIdx >= 0 && audioStream()->codecpar->codec_id == AV_CODEC_ID_AAC;
	bool audio = !aac || mAudioFmt;
	int n = 0;
	while (!key || !audio)
	{
		int r = av_read_frame(mFmt, pkt);
		if (r < 0)
		{
			error = r;
			av_packet_free(&pkt);
			return false;
		}
		if (++n > PREROLL_MAX_PACKETS)
		{
			hal_info("%s: no keyframe in %d packets\n", __func__, PREROLL_MAX_PACKETS);
			error = AVERROR_INVALIDDATA;
			av_packet_free(&pkt);
			return false;
		}
		pkt->time_base = mFmt->streams[pkt->stream_index]->time_base;
		int64_t pts = packetPts(pkt);
		if (!key && pkt->stream_index == mVideoIdx && (pkt->flags & AV_PKT_FLAG_KEY) && pts != AV_NOPTS_VALUE)
		{
			key = true;
			mStartPts = pts;
		}
		if (key && (pkt->stream_index == mVideoIdx || (!mAudioFmt && pkt->stream_index == mAudioIdx)))
		{
			if (!audio && pkt->stream_index == mAudioIdx)
			{
				audio = true;
				mAdts = pkt->size > 2 && pkt->data[0] == 0xff && (pkt->data[1] & 0xf0) == 0xf0;
			}
			mBytes += pkt->size;
			mPreroll.push_back(av_packet_clone(pkt));
		}
		av_packet_unref(pkt);
	}
	/* a sound of its own: its first packet tells ADTS from raw */
	while (mAudioFmt && aac && mAudioPreroll.empty())
	{
		int r = av_read_frame(mAudioFmt, pkt);
		if (r < 0)
		{
			error = r;
			av_packet_free(&pkt);
			return false;
		}
		if (pkt->stream_index == mAudioIdx)
		{
			pkt->time_base = mAudioFmt->streams[mAudioIdx]->time_base;
			mAdts = pkt->size > 2 && pkt->data[0] == 0xff && (pkt->data[1] & 0xf0) == 0xf0;
			mBytes += pkt->size;
			mAudioPreroll.push_back(av_packet_clone(pkt));
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	return true;
}

std::string cStarfishPlayer::loadPayload(const std::string &videoCodec, const std::string &audioCodec)
{
	AVStream *vs = mFmt->streams[mVideoIdx];
	AVCodecParameters *v = vs->codecpar;
	AVRational fps = vs->avg_frame_rate.num > 0 && vs->avg_frame_rate.den > 0 ? vs->avg_frame_rate : vs->r_frame_rate;
	const char *env = getenv("APPID");
	std::string appId = env ? env : "org.tuxbox.neutrino";
	const char *window = glfb_webos_window_id();
	char b[1024];

	std::string contents = "{\"codec\":{\"video\":\"" + videoCodec + "\"";
	if (!audioCodec.empty())
		contents += ",\"audio\":\"" + audioCodec + "\"";
	contents += "}";
	if (mAudioIdx >= 0)
	{
		AVCodecParameters *a = audioStream()->codecpar;
		int channels = a->ch_layout.nb_channels;
		double khz = a->sample_rate / 1000.0;
		if (a->codec_id == AV_CODEC_ID_AAC)
		{
			int profile = a->profile >= 0 ? a->profile + 1 : 2;
			snprintf(b, sizeof(b), ",\"aacInfo\":{\"channels\":%d,\"profile\":%d,\"format\":\"%s\",\"frequency\":%.3f}",
				 channels, profile, mAdts ? "adts" : "raw", khz);
			contents += b;
		}
		else if (a->codec_id == AV_CODEC_ID_EAC3)
		{
			snprintf(b, sizeof(b), ",\"ac3PlusInfo\":{\"channels\":%d,\"frequency\":%.3f}", channels, khz);
			contents += b;
		}
	}
	snprintf(b, sizeof(b),
		 ",\"esInfo\":{\"pauseAtDecodeTime\":true,\"seperatedPTS\":true,\"ptsToDecode\":%" PRId64
		 ",\"videoWidth\":%d,\"videoHeight\":%d", mStartPts, v->width, v->height);
	contents += b;
	if (fps.num > 0 && fps.den > 0)
	{
		snprintf(b, sizeof(b), ",\"videoFpsValue\":%d,\"videoFpsScale\":%d", fps.num, fps.den);
		contents += b;
	}
	contents += "},\"format\":\"RAW\",\"provider\":\"" + appId + "\"}";

	snprintf(b, sizeof(b),
		 "\"bufferingCtrInfo\":{\"preBufferByte\":0,\"bufferMinLevel\":0,\"bufferMaxLevel\":0,"
		 "\"qBufferLevelVideo\":0,\"srcBufferLevelVideo\":{\"minimum\":0,\"maximum\":%u},"
		 "\"qBufferLevelAudio\":0,\"srcBufferLevelAudio\":{\"minimum\":0,\"maximum\":%u}}",
		 VIDEO_SRC_BUFFER, AUDIO_SRC_BUFFER);
	std::string buffering = b;

	std::string option = "{\"appId\":\"" + appId + "\"";
	if (window)
		option += std::string(",\"windowId\":\"") + window + "\"";
	if (mAudioIdx < 0)
		option += ",\"needAudio\":false";
	option += ",\"externalStreamingInfo\":{\"contents\":" + contents + "," + buffering + "}";
	option += ",\"transmission\":{\"contentsType\":\"LIVE\",\"trickType\":\"client-side\"}";
	option += ",\"seekMode\":\"keep-rate\"}";

	return "{\"args\":[{\"mediaTransportType\":\"BUFFERSTREAM\",\"option\":" + option + "}]}";
}

int cStarfishPlayer::openInput(AVFormatContext *&fmt, const std::string &url, const std::string &userAgent,
			       const std::vector<std::string> &headerFields)
{
	if (!fmt)
		fmt = avformat_alloc_context();
	fmt->interrupt_callback.callback = interruptCb;
	fmt->interrupt_callback.opaque = this;
	AVDictionary *opts = NULL;
	if (!userAgent.empty())
		av_dict_set(&opts, "user_agent", userAgent.c_str(), 0);
	std::string headers;
	for (const std::string &f : headerFields)
		headers += f + "\r\n";
	if (!headers.empty())
		av_dict_set(&opts, "headers", headers.c_str(), 0);
	av_dict_set(&opts, "reconnect", "1", 0);
	av_dict_set(&opts, "reconnect_streamed", "1", 0);
	av_dict_set(&opts, "rw_timeout", "15000000", 0);
	int r = avformat_open_input(&fmt, url.c_str(), NULL, &opts);
	av_dict_free(&opts);
	if (r < 0)
		fmt = NULL;
	return r;
}

cStarfishPlayer::Result cStarfishPlayer::start(const std::string &url, const std::string &audioUrl, const std::string &userAgent,
					       const std::vector<std::string> &headerFields, int &error, int startMs, bool growing)
{
	stop();
	error = 0;
	mStop = false;
	mEof = false;
	mTimeSet = false;
	mFramePts = AV_NOPTS_VALUE;
	mDemuxPts = AV_NOPTS_VALUE;
	mBytes = 0;
	mAdts = false;
	mAudioUrl = growing ? "" : audioUrl;
	mUserAgent = userAgent;
	mHeaderFields = headerFields;
	if (mAbort)
		return ABORTED;

	mSeekReq = false;
	mWaitKey = false;
	mTsOffset = mTsPrevOffset = 0;
	mLastDts[0] = mLastDts[1] = mLastDts[2] = AV_NOPTS_VALUE;
	mAudioSeekReq = false;
	if (growing)
	{
		mFmt = avformat_alloc_context();
		mFd = open(url.c_str(), O_RDONLY | O_CLOEXEC);
		if (mFd < 0)
		{
			error = AVERROR(errno);
			avformat_free_context(mFmt);
			mFmt = NULL;
			return FAILED;
		}
		unsigned char *buf = (unsigned char *)av_malloc(IO_BUFFER);
		mIo = avio_alloc_context(buf, IO_BUFFER, 0, this, ioRead, NULL, ioSeek);
		mFmt->pb = mIo;
		mFmt->flags |= AVFMT_FLAG_CUSTOM_IO;
	}
	int r = openInput(mFmt, url, userAgent, headerFields);
	if (r < 0)
	{
		error = r;
		hal_info("%s: open: %s\n", __func__, averr(r).c_str());
		stop();
		return mAbort ? ABORTED : FAILED;
	}
	/* HLS: choose the variant before probing, so that only it is read;
	 * probing all of them would not get far enough into the best one to
	 * know its sound */
	int variant = bestVideo();
	if (variant >= 0)
	{
		AVProgram *prog = av_find_program_from_stream(mFmt, NULL, variant);
		if (prog)
		{
			for (unsigned i = 0; i < mFmt->nb_streams; i++)
			{
				bool keep = false;
				for (unsigned k = 0; k < prog->nb_stream_indexes; k++)
					keep |= prog->stream_index[k] == i;
				mFmt->streams[i]->discard = keep ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
			}
		}
	}
	mFmt->probesize = 2 << 20;
	mFmt->max_analyze_duration = 3 * AV_TIME_BASE;
	r = avformat_find_stream_info(mFmt, NULL);
	if (r < 0)
	{
		error = r;
		hal_info("%s: stream info: %s\n", __func__, averr(r).c_str());
		stop();
		return mAbort ? ABORTED : FAILED;
	}
	mVideoIdx = variant >= 0 ? variant : av_find_best_stream(mFmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (!audioUrl.empty() && !growing)
	{
		/* the sound as a stream of its own, as YouTube has it */
		r = openInput(mAudioFmt, audioUrl, userAgent, headerFields);
		if (r >= 0)
			r = avformat_find_stream_info(mAudioFmt, NULL);
		if (r < 0)
		{
			error = r;
			hal_info("%s: sound: %s\n", __func__, averr(r).c_str());
			stop();
			return mAbort ? ABORTED : FAILED;
		}
		mAudioIdx = av_find_best_stream(mAudioFmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	}
	else
	{
		mAudioIdx = av_find_best_stream(mFmt, AVMEDIA_TYPE_AUDIO, -1, mVideoIdx, NULL, 0);
		/* HLS may carry the sound as a rendition of its own, outside the
		 * program of the video variant */
		if (mAudioIdx < 0)
			mAudioIdx = av_find_best_stream(mFmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	}

	std::string videoCodec, audioCodec;
	if (!supported(videoCodec, audioCodec))
	{
		stop();
		return UNSUPPORTED;
	}
	for (unsigned i = 0; i < mFmt->nb_streams; i++)
		mFmt->streams[i]->discard = ((int)i == mVideoIdx || (!mAudioFmt && (int)i == mAudioIdx)) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
	for (unsigned i = 0; mAudioFmt && i < mAudioFmt->nb_streams; i++)
		mAudioFmt->streams[i]->discard = (int)i == mAudioIdx ? AVDISCARD_DEFAULT : AVDISCARD_ALL;

	mFileStart = mFmt->start_time != AV_NOPTS_VALUE ? av_rescale(mFmt->start_time, 1000, 1) : 0;
	if (startMs > 0)
	{
		int64_t ts = (mFmt->start_time != AV_NOPTS_VALUE ? mFmt->start_time : 0) + (int64_t)startMs * 1000;
		if (av_seek_frame(mFmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0)
			hal_info("%s: cannot start at %d ms\n", __func__, startMs);
	}

	if (!preroll(error))
	{
		hal_info("%s: preroll: %s\n", __func__, averr(error).c_str());
		stop();
		return mAbort ? ABORTED : FAILED;
	}

	std::string payload = loadPayload(videoCodec, audioCodec);
	hal_debug("%s: %s\n", __func__, payload.c_str());
	{
		std::lock_guard<std::mutex> lk(mEventLock);
		mLoadDone = false;
		mUnloadDone = false;
	}
	/* the library reports some failures as C++ exceptions */
	const char *step = "create";
	try
	{
		mApi = new StarfishMediaAPIs(nullptr);
		step = "notifyForeground";
		if (!mApi->notifyForeground())
			hal_info("%s: notifyForeground failed\n", __func__);
		step = "Load";
		if (!mApi->Load(payload.c_str(), &cStarfishPlayer::eventCb, this))
		{
			hal_info("%s: Load failed\n", __func__);
			stop();
			return FAILED;
		}
	}
	catch (const std::exception &e)
	{
		hal_info("%s: %s: %s\n", __func__, step, e.what());
		stop();
		return FAILED;
	}
	{
		std::unique_lock<std::mutex> lk(mEventLock);
		mEventCond.wait_for(lk, 5s, [this] { return mLoadDone || mAbort; });
		if (!mLoadDone)
		{
			hal_info("%s: the pipeline did not load\n", __func__);
			lk.unlock();
			stop();
			return mAbort ? ABORTED : FAILED;
		}
	}
	mLoaded = true;
	if (glfb_priv)
		glfb_priv->setExternalVideo(true);
	glfb_webos_set_video_rect(mFmt->streams[mVideoIdx]->codecpar->width, mFmt->streams[mVideoIdx]->codecpar->height, 0, 0, 0, 0);

	mVideoThread = std::thread(&cStarfishPlayer::feedLoop, this, 1);
	if (mAudioIdx >= 0)
		mAudioThread = std::thread(&cStarfishPlayer::feedLoop, this, 2);
	mDemuxThread = std::thread(&cStarfishPlayer::demuxLoop, this);
	if (mAudioFmt)
		mAudioDemuxThread = std::thread(&cStarfishPlayer::audioDemuxLoop, this);
	hal_info("%s: %s %dx%d%s%s from %.3f s\n", __func__, videoCodec.c_str(),
		 mFmt->streams[mVideoIdx]->codecpar->width, mFmt->streams[mVideoIdx]->codecpar->height,
		 audioCodec.empty() ? "" : ", ", audioCodec.c_str(), mStartPts / 1e9);
	if (mAudioFmt)
		hal_info("%s: the sound from a stream of its own\n", __func__);
	return OK;
}

void cStarfishPlayer::stop()
{
	mStop = true;
	mQueueCond.notify_all();
	if (mDemuxThread.joinable())
		mDemuxThread.join();
	if (mAudioDemuxThread.joinable())
		mAudioDemuxThread.join();
	if (mVideoThread.joinable())
		mVideoThread.join();
	if (mAudioThread.joinable())
		mAudioThread.join();
	if (mApi)
	{
		bool loaded;
		{
			std::lock_guard<std::mutex> lk(mEventLock);
			loaded = mLoadDone;
		}
		if (loaded)
		{
			try
			{
				if (!mApi->Unload())
					hal_info("%s: Unload failed\n", __func__);
			}
			catch (const std::exception &e)
			{
				hal_info("%s: Unload: %s\n", __func__, e.what());
			}
			std::unique_lock<std::mutex> lk(mEventLock);
			if (!mEventCond.wait_for(lk, 1s, [this] { return mUnloadDone; }))
				hal_info("%s: no UNLOADCOMPLETED\n", __func__);
		}
		delete mApi;
		mApi = NULL;
	}
	if (mLoaded && glfb_priv)
		glfb_priv->setExternalVideo(false);
	mLoaded = false;
	emptyQueues();
	for (std::vector<AVPacket *> *v : { &mPreroll, &mAudioPreroll })
	{
		for (AVPacket *pkt : *v)
			av_packet_free(&pkt);
		v->clear();
	}
	if (mFmt)
		avformat_close_input(&mFmt);
	if (mAudioFmt)
		avformat_close_input(&mAudioFmt);
	if (mIo)
	{
		av_freep(&mIo->buffer);
		avio_context_free(&mIo);
	}
	if (mFd >= 0)
	{
		close(mFd);
		mFd = -1;
	}
}

void cStarfishPlayer::eventCb(int type, int64_t numValue, const char *strValue, void *data)
{
	static_cast<cStarfishPlayer *>(data)->event(type, numValue, strValue);
}

void cStarfishPlayer::event(int type, int64_t numValue, const char *strValue)
{
	switch (type)
	{
		case SF_EVENT_FRAMEREADY:
			mFramePts = numValue;
			return;
		case SF_EVENT_LOADCOMPLETED:
			hal_info("%s: loaded, media id %s\n", __func__, mApi->getMediaID());
			if (!mApi->Play())
				hal_info("%s: Play failed\n", __func__);
			normalRate();
			{
				std::lock_guard<std::mutex> lk(mEventLock);
				mLoadDone = true;
			}
			break;
		case SF_EVENT_UNLOADCOMPLETED:
		{
			std::lock_guard<std::mutex> lk(mEventLock);
			mUnloadDone = true;
			break;
		}
		case SF_EVENT_ENDOFSTREAM:
			hal_info("%s: end of stream\n", __func__);
			mEof = true;
			break;
		case SF_EVENT_INT_ERROR:
		case SF_EVENT_STR_ERROR:
			hal_info("%s: error %" PRId64 " %s\n", __func__, numValue, strValue ? strValue : "");
			break;
		case SF_EVENT_PLAYING:
		case SF_EVENT_PAUSED:
		case SF_EVENT_STR_VIDEO_INFO:
		case SF_EVENT_STR_AUDIO_INFO:
			hal_info("%s: 0x%x %s\n", __func__, type, strValue ? strValue : "");
			break;
		case SF_EVENT_DROPPED_FRAME:
			hal_debug("%s: %" PRId64 " frames dropped\n", __func__, numValue);
			return;
		default:
			hal_debug("%s: 0x%x %" PRId64 " %s\n", __func__, type, numValue, strValue ? strValue : "");
			return;
	}
	mEventCond.notify_all();
}

void cStarfishPlayer::push(Queue &q, AVPacket *pkt)
{
	size_t max = &q == &mVideoQ ? VIDEO_QUEUE_MAX : AUDIO_QUEUE_MAX;
	std::unique_lock<std::mutex> lk(mQueueLock);
	mQueueCond.wait(lk, [&] { return mStop || mSeekReq || q.bytes < max; });
	if (mStop || mSeekReq)
	{
		av_packet_free(&pkt);
		return;
	}
	q.bytes += pkt->size;
	q.packets.push_back(pkt);
	mQueueCond.notify_all();
}

void cStarfishPlayer::closeQueue(Queue &q)
{
	std::lock_guard<std::mutex> lk(mQueueLock);
	q.done = true;
	mQueueCond.notify_all();
}

void cStarfishPlayer::closeQueues()
{
	closeQueue(mVideoQ);
	closeQueue(mAudioQ);
}

void cStarfishPlayer::emptyQueues()
{
	std::lock_guard<std::mutex> lk(mQueueLock);
	for (Queue *q : { &mVideoQ, &mAudioQ })
	{
		for (AVPacket *pkt : q->packets)
			av_packet_free(&pkt);
		q->packets.clear();
		q->bytes = 0;
		q->done = false;
		q->eosSent = false;
	}
	mQueueCond.notify_all();
}

/* the pipeline is emptied, ffmpeg jumps, and feeding begins anew at the
 * next keyframe */
void cStarfishPlayer::doSeek()
{
	int64_t target = mSeekTarget;
	mSeekReq = false;
	mFlushing = true;
	emptyQueues();
	{
		std::lock_guard<std::mutex> v(mFeedLock[1]), a(mFeedLock[2]);
		try
		{
			if (!mApi->flush())
				hal_info("%s: flush failed\n", __func__);
		}
		catch (const std::exception &e)
		{
			hal_info("%s: flush: %s\n", __func__, e.what());
		}
		mTimeSet = false;
		mFramePts = AV_NOPTS_VALUE;
		mTsOffset = mTsPrevOffset = 0;
		mLastDts[0] = mLastDts[1] = mLastDts[2] = AV_NOPTS_VALUE;
		mEof = false;
		mStartPts = target;
	}
	mFlushing = false;
	int r = av_seek_frame(mFmt, -1, av_rescale(target, AV_TIME_BASE, 1000000000), AVSEEK_FLAG_BACKWARD);
	if (r < 0)
		hal_info("%s: %s\n", __func__, averr(r).c_str());
	if (mAudioFmt)
	{
		mAudioSeekReq = true;
		mQueueCond.notify_all();
	}
	mWaitKey = true;
	hal_info("%s: to %.3f s\n", __func__, (target - mFileStart) / 1e9);
}

void cStarfishPlayer::demuxLoop()
{
	for (AVPacket *pkt : mPreroll)
		push(pkt->stream_index == mVideoIdx ? mVideoQ : mAudioQ, pkt);
	mPreroll.clear();
	bool sound = !mAudioFmt;

	AVPacket *pkt = av_packet_alloc();
	bool ended = false;
	while (!mStop)
	{
		if (mSeekReq)
		{
			doSeek();
			ended = false;
		}
		if (ended)
		{
			/* at the end of a file: wait for a jump back, or the end */
			std::unique_lock<std::mutex> lk(mQueueLock);
			mQueueCond.wait_for(lk, 200ms, [this] { return mStop || mSeekReq; });
			continue;
		}
		int r = av_read_frame(mFmt, pkt);
		if (r == AVERROR(EAGAIN))
		{
			if (!mSeekReq)
				std::this_thread::sleep_for(10ms);
			continue;
		}
		if (r < 0)
		{
			if (!mStop)
				hal_info("%s: %s\n", __func__, averr(r).c_str());
			if (sound)
				closeQueues();
			else
				closeQueue(mVideoQ);
			ended = true;
			continue;
		}
		pkt->time_base = mFmt->streams[pkt->stream_index]->time_base;
		if (pkt->stream_index == mVideoIdx || (sound && pkt->stream_index == mAudioIdx))
		{
			if (mWaitKey)
			{
				if (pkt->stream_index != mVideoIdx || !(pkt->flags & AV_PKT_FLAG_KEY))
				{
					av_packet_unref(pkt);
					continue;
				}
				mWaitKey = false;
			}
			smoothJumps(pkt, pkt->stream_index == mVideoIdx ? 1 : 2);
			int64_t pts = packetPts(pkt);
			if (pts != AV_NOPTS_VALUE)
				mDemuxPts = pts;
			mBytes += pkt->size;
			push(pkt->stream_index == mVideoIdx ? mVideoQ : mAudioQ, av_packet_clone(pkt));
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	if (sound)
		closeQueues();
	else
		closeQueue(mVideoQ);
}

bool cStarfishPlayer::reopenAudio()
{
	for (int tries = 0; tries < 5 && !mStop; tries++)
	{
		if (tries)
			std::this_thread::sleep_for(1s);
		AVFormatContext *fmt = NULL;
		int r = openInput(fmt, mAudioUrl, mUserAgent, mHeaderFields);
		if (r >= 0)
			r = avformat_find_stream_info(fmt, NULL);
		int idx = r >= 0 ? av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0) : r;
		if (idx >= 0)
		{
			for (unsigned i = 0; i < fmt->nb_streams; i++)
				fmt->streams[i]->discard = (int)i == idx ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
			avformat_close_input(&mAudioFmt);
			mAudioFmt = fmt;
			mAudioIdx = idx;
			hal_info("%s: the sound is open again\n", __func__);
			return true;
		}
		if (fmt)
			avformat_close_input(&fmt);
		hal_info("%s: %s\n", __func__, averr(idx).c_str());
	}
	return false;
}

/* the sound from a stream of its own, read beside the video */
void cStarfishPlayer::audioDemuxLoop()
{
	for (AVPacket *pkt : mAudioPreroll)
		push(mAudioQ, pkt);
	mAudioPreroll.clear();

	AVPacket *pkt = av_packet_alloc();
	bool ended = false;
	bool first = true;
	while (!mStop)
	{
		if (mAudioSeekReq && !mSeekReq)
		{
			mAudioSeekReq = false;
			int r = av_seek_frame(mAudioFmt, -1, av_rescale(mStartPts, AV_TIME_BASE, 1000000000), AVSEEK_FLAG_BACKWARD);
			if (r < 0)
				hal_info("%s: %s\n", __func__, averr(r).c_str());
			ended = false;
		}
		if (ended || mSeekReq)
		{
			std::unique_lock<std::mutex> lk(mQueueLock);
			mQueueCond.wait_for(lk, 200ms, [this] { return mStop || mAudioSeekReq; });
			continue;
		}
		int r = av_read_frame(mAudioFmt, pkt);
		if (r == AVERROR(EAGAIN))
		{
			std::this_thread::sleep_for(10ms);
			continue;
		}
		if (r < 0)
		{
			if (!mStop)
				hal_info("%s: %s\n", __func__, averr(r).c_str());
			/* a live stream has no end: its sound is opened anew */
			if (!mStop && mFmt->duration <= 0 && reopenAudio())
				continue;
			closeQueue(mAudioQ);
			ended = true;
			continue;
		}
		if (pkt->stream_index == mAudioIdx)
		{
			pkt->time_base = mAudioFmt->streams[mAudioIdx]->time_base;
			if (first)
			{
				first = false;
				int64_t t = packetPts(pkt);
				if (t != AV_NOPTS_VALUE)
					hal_info("%s: the sound starts %" PRId64 " ms from the picture\n", __func__, (t - mStartPts) / 1000000);
			}
			smoothJumps(pkt, 2);
			mBytes += pkt->size;
			push(mAudioQ, av_packet_clone(pkt));
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	closeQueue(mAudioQ);
}

bool cStarfishPlayer::feed(AVPacket *pkt, int esData)
{
	int64_t pts = packetPts(pkt);
	if (pts == AV_NOPTS_VALUE || pkt->size <= 0)
		return true;
	if (esData == 1 && !mTimeSet)
	{
		char t[64];
		snprintf(t, sizeof(t), "{\"position\":%" PRId64 "}", pts);
		if (!mApi->setTimeToDecode(t))
			hal_info("%s: setTimeToDecode failed\n", __func__);
		mTimeSet = true;
	}
	/* once pictures come, stay close to them: the pipeline keeps audio and
	 * video in step only for what lies near the clock */
	for (;;)
	{
		int64_t frame = mFramePts;
		if (mStop || mFlushing || frame == AV_NOPTS_VALUE || pts - frame <= MAX_FEED_AHEAD)
			break;
		std::this_thread::sleep_for(10ms);
	}
	if (mStop || mFlushing)
		return false;
	char json[192];
	snprintf(json, sizeof(json), "{\"bufferAddr\":\"0x%" PRIxPTR "\",\"bufferSize\":%d,\"pts\":%" PRId64 ",\"esData\":%d}",
		 (uintptr_t)pkt->data, pkt->size, pts, esData);
	while (!mStop && !mFlushing)
	{
		std::string r;
		try
		{
			r = mApi->Feed(json);
		}
		catch (const std::exception &e)
		{
			hal_info("%s: %s\n", __func__, e.what());
			return false;
		}
		if (r.find("Ok") != std::string::npos)
			return true;
		if (r.find("BufferFull") != std::string::npos)
		{
			std::this_thread::sleep_for(10ms);
			continue;
		}
		hal_info("%s: %s packet: %s\n", __func__, esData == 1 ? "video" : "audio", r.c_str());
		return false;
	}
	return false;
}

void cStarfishPlayer::feedLoop(int esData)
{
	Queue &q = esData == 1 ? mVideoQ : mAudioQ;
	while (true)
	{
		AVPacket *pkt = NULL;
		bool eos = false;
		{
			std::unique_lock<std::mutex> lk(mQueueLock);
			mQueueCond.wait(lk, [&] { return mStop || !q.packets.empty() || (q.done && !q.eosSent); });
			if (mStop)
				break;
			if (!q.packets.empty())
			{
				pkt = q.packets.front();
				q.packets.pop_front();
				q.bytes -= pkt->size;
				mQueueCond.notify_all();
			}
			else
			{
				q.eosSent = true;
				eos = true;
			}
		}
		std::lock_guard<std::mutex> f(mFeedLock[esData]);
		if (eos)
		{
			/* the end of a file: let the pipeline play out what it has */
			if (esData == 1 && !mFlushing && !mApi->pushEOS())
				hal_info("%s: pushEOS failed\n", __func__);
			continue;
		}
		if (!mFlushing)
			feed(pkt, esData);
		av_packet_free(&pkt);
	}
}

/* with trickType client-side the rate is ours to set: without it the
 * pipeline runs fast to catch up with the live edge and mutes the sound */
void cStarfishPlayer::normalRate()
{
	if (!mApi->SetPlayRate("{\"audioOutput\":true,\"playRate\":1.0}"))
		hal_info("%s: SetPlayRate failed\n", __func__);
}

bool cStarfishPlayer::setPaused(bool paused)
{
	if (!mApi || !mLoaded)
		return false;
	if (paused)
		return mApi->Pause();
	bool ok = mApi->Play();
	normalRate();
	return ok;
}

bool cStarfishPlayer::seek(int64_t ms, bool absolute)
{
	if (!mLoaded)
		return false;
	int64_t frame = mFramePts;
	int64_t now = frame != AV_NOPTS_VALUE ? frame : mStartPts;
	int64_t target = (absolute ? mFileStart : now) + ms * 1000000;
	if (target < mFileStart)
		target = mFileStart;
	mSeekTarget = target;
	mSeekReq = true;
	mQueueCond.notify_all();
	return true;
}

void cStarfishPlayer::setTrick(bool on)
{
	if (!mApi || !mLoaded)
		return;
	if (on)
		mApi->SetPlayRate("{\"audioOutput\":false,\"playRate\":1.0}");
	else
		normalRate();
}

bool cStarfishPlayer::position(int64_t &posMs, int64_t &durMs)
{
	if (!mLoaded)
		return false;
	int64_t frame = mFramePts;
	posMs = ((frame == AV_NOPTS_VALUE ? mStartPts : frame) - mFileStart) / 1000000;
	if (posMs < 0)
		posMs = 0;
	durMs = mFmt && mFmt->duration > 0 ? mFmt->duration / 1000 : 0;
	return true;
}

uint64_t cStarfishPlayer::readCount()
{
	return mBytes;
}

int cStarfishPlayer::bufferedMs()
{
	int64_t frame = mFramePts, demux = mDemuxPts;
	if (frame == AV_NOPTS_VALUE || demux == AV_NOPTS_VALUE || demux < frame)
		return 0;
	return (demux - frame) / 1000000;
}

void cStarfishPlayer::audioTracks(std::vector<Track> &tracks)
{
	tracks.clear();
	AVFormatContext *fmt = mAudioFmt ? mAudioFmt : mFmt;
	if (!fmt)
		return;
	for (unsigned i = 0; i < fmt->nb_streams; i++)
	{
		AVStream *s = fmt->streams[i];
		if (s->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
			continue;
		Track t;
		t.id = i;
		switch (s->codecpar->codec_id)
		{
			case AV_CODEC_ID_AC3:
			case AV_CODEC_ID_EAC3:
				t.ac3flag = 1;
				break;
			case AV_CODEC_ID_MP2:
			case AV_CODEC_ID_MP3:
				t.ac3flag = 4;
				break;
			case AV_CODEC_ID_AAC:
			case AV_CODEC_ID_AAC_LATM:
				t.ac3flag = 5;
				break;
			case AV_CODEC_ID_DTS:
				t.ac3flag = 6;
				break;
			default:
				t.ac3flag = 0;
		}
		AVDictionaryEntry *e = av_dict_get(s->metadata, "language", NULL, 0);
		t.language = e ? e->value : "unk";
		tracks.push_back(t);
	}
}

void cStarfishPlayer::metadata(std::vector<std::string> &keys, std::vector<std::string> &values)
{
	keys.clear();
	values.clear();
	if (!mFmt)
		return;
	const AVDictionaryEntry *e = NULL;
	while ((e = av_dict_iterate(mFmt->metadata, e)))
	{
		keys.push_back(e->key);
		values.push_back(e->value);
	}
}
