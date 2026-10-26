/*
 * Playback through the box's own decoders: ffmpeg demuxes, the
 * elementary streams go as PES into the video and audio devices
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#include <config.h>

#include <algorithm>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/dvb/audio.h>
#include <linux/dvb/video.h>

#ifndef AUDIO_GET_PTS
#define AUDIO_GET_PTS _IOR('o', 19, __u64)
#endif

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

#include "dvb_player.h"
#include "audio_lib.h"
#include "video_lib.h"
#include "hal_debug.h"
#include "proc_tools.h"

#define hal_debug(args...) _hal_debug(HAL_DEBUG_PLAYBACK, this, args)
#define hal_info(args...)  _hal_info(HAL_DEBUG_PLAYBACK, this, args)

extern cAudio *audioDecoder;
extern cVideo *videoDecoder;

namespace
{

/* VIDEO_SET_STREAMTYPE and AUDIO_SET_BYPASS_MODE of this decoder */
enum
{
	STREAM_MPEG2 = 0,
	STREAM_H264 = 1,
	STREAM_MPEG1 = 6,
	STREAM_H265 = 7,
	STREAM_H265_ALT = 22,
	BYPASS_AC3 = 0,
	BYPASS_MPEG = 1,
	BYPASS_MP3 = 0xa,
	BYPASS_AAC_ADTS = 0xb,
	BYPASS_EAC3 = 0x22,
	BYPASS_EAC3_ALT = 7
};

const uint8_t PES_VIDEO = 0xe0;
const uint8_t PES_AUDIO = 0xc0;
/* the picture start the decoder looks for after the PES header of H.264 and H.265 */
const uint8_t FAKE_START_CODE = 0x31;

const size_t VIDEO_QUEUE_BYTES = 4 << 20;
const size_t AUDIO_QUEUE_BYTES = 1 << 20;
/* how long the end of a recording that is still written waits for more */
const int GROW_WAIT_MS = 3000;
const int HELD_LEAD_MS = 2000;
const char *FALLBACK_RATE = "/proc/stb/vmpeg/0/fallback_framerate";
#if BOXMODEL_HD51 || BOXMODEL_BRE2ZE4K || BOXMODEL_H7
/* the BCM7251S decodes H.264 only up to HD; beyond, picture and clock stand still */
const int H264_MAX_WIDTH = 1920;
const int H264_MAX_HEIGHT = 1088;
#else
const int H264_MAX_WIDTH = 0;
const int H264_MAX_HEIGHT = 0;
#endif

void pesHeader(std::vector<uint8_t> &out, int size, uint8_t streamId, int64_t pts, bool fakeStart)
{
	int len = 0;
	if (size > 0)
	{
		len = size + 3 + (pts >= 0 ? 5 : 0) + (fakeStart ? 5 : 0);
		if (len > 65535)
			len = 0;
	}
	out.push_back(0x00);
	out.push_back(0x00);
	out.push_back(0x01);
	out.push_back(streamId);
	out.push_back((len >> 8) & 0xff);
	out.push_back(len & 0xff);
	out.push_back(0x80);
	out.push_back(pts >= 0 ? 0x80 : 0x00);
	out.push_back(pts >= 0 ? 5 : 0);
	if (pts >= 0)
	{
		out.push_back(0x21 | ((pts >> 29) & 0x0e));
		out.push_back((pts >> 22) & 0xff);
		out.push_back(0x01 | ((pts >> 14) & 0xfe));
		out.push_back((pts >> 7) & 0xff);
		out.push_back(0x01 | ((pts << 1) & 0xfe));
	}
	if (fakeStart)
	{
		out.push_back(0x00);
		out.push_back(0x00);
		out.push_back(0x01);
		out.push_back(FAKE_START_CODE);
		out.push_back(0x00);
	}
}

/* a NAL unit of the given type (H.264) or of a parameter set (H.265) behind a start code */
bool hasParameterSet(const uint8_t *d, int len, bool hevc)
{
	for (int i = 0; i + 3 < len && i < 256; i++)
	{
		if (d[i] || d[i + 1] || d[i + 2] != 1)
			continue;
		uint8_t nal = d[i + 3];
		if (hevc ? ((nal >> 1) & 0x3f) == 32 : (nal & 0x1f) == 7)
			return true;
	}
	return false;
}

int adtsRateIndex(int rate)
{
	static const int rates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
	for (int i = 0; i < 13; i++)
		if (rates[i] == rate)
			return i;
	return 3;
}

int trackId(const AVStream *st)
{
	return st->id > 0 ? st->id : st->index;
}

int ac3flag(enum AVCodecID id)
{
	switch (id)
	{
		case AV_CODEC_ID_AC3: return 1;
		case AV_CODEC_ID_MP2: return 3;
		case AV_CODEC_ID_MP3: return 4;
		case AV_CODEC_ID_AAC: return 5;
		case AV_CODEC_ID_DTS: return 6;
		case AV_CODEC_ID_EAC3: return 7;
		default: return 0;
	}
}

int bypassFor(enum AVCodecID id)
{
	switch (id)
	{
		case AV_CODEC_ID_AC3: return BYPASS_AC3;
		case AV_CODEC_ID_EAC3: return BYPASS_EAC3;
		case AV_CODEC_ID_MP2: return BYPASS_MPEG;
		case AV_CODEC_ID_MP3: return BYPASS_MP3;
		case AV_CODEC_ID_AAC: return BYPASS_AAC_ADTS;
		default: return -1;
	}
}

int streamTypeFor(enum AVCodecID id)
{
	switch (id)
	{
		case AV_CODEC_ID_H264: return STREAM_H264;
		case AV_CODEC_ID_HEVC: return STREAM_H265;
		case AV_CODEC_ID_MPEG2VIDEO: return STREAM_MPEG2;
		case AV_CODEC_ID_MPEG1VIDEO: return STREAM_MPEG1;
		default: return -1;
	}
}

/* the rate the decoder takes for a stream that does not tell its own */
int fallbackRate(AVRational r)
{
	static const int rates[] = { 23976, 24000, 25000, 29970, 30000, 50000, 59940, 60000 };
	if (r.num <= 0 || r.den <= 0)
		return 0;
	int64_t want = av_rescale(r.num, 1000, r.den);
	int best = rates[0];
	for (unsigned i = 1; i < sizeof(rates) / sizeof(rates[0]); i++)
		if (llabs(want - rates[i]) < llabs(want - best))
			best = rates[i];
	return best;
}

} /* namespace */

cDvbPlayer::cDvbPlayer()
	: mAbort(false), mStop(false), mPlaying(false), mEof(false), mPaused(false), mTrick(false),
	  mBytes(0), mSeekReq(false), mSeekTarget(0), mGrowing(false), mFollow(false), mDemuxDone(false), mAudioFrom(-1), mFd(-1),
	  mIo(NULL), mFmt(NULL), mAudioFmt(NULL), mAudioSeekReq(false), mVideoIdx(-1), mAudioIdx(-1), mAudioId(-1), mWantAudio(-1),
	  mVideoType(-1), mAudioType(-1), mAdts(false), mSpsSeen(false), mStart90(0),
	  mVideoFd(-1), mAudioFd(-1), mVideoFlags(0), mAudioFlags(0), mGen(0), mVideoGen(0), mAudioGen(0)
{
	mVideoQ.limit = VIDEO_QUEUE_BYTES;
	mAudioQ.limit = AUDIO_QUEUE_BYTES;
}

cDvbPlayer::~cDvbPlayer()
{
	stop();
}

void cDvbPlayer::reset()
{
	mAbort = false;
}

/* set under the queue lock: a thread that found it unset and is about
 * to wait would else miss the wake-up and wait for ever */
void cDvbPlayer::setAndWake(std::atomic<bool> &flag)
{
	{
		std::lock_guard<std::mutex> lock(mQueueLock);
		flag = true;
	}
	mQueueCond.notify_all();
}

void cDvbPlayer::abort()
{
	setAndWake(mAbort);
}

int cDvbPlayer::interruptCb(void *opaque)
{
	cDvbPlayer *p = (cDvbPlayer *)opaque;
	return p->mAbort || p->mStop;
}

int cDvbPlayer::ioRead(void *opaque, uint8_t *buf, int size)
{
	cDvbPlayer *p = (cDvbPlayer *)opaque;
	int waited = 0;
	for (;;)
	{
		ssize_t n = read(p->mFd, buf, size);
		if (n > 0)
			return (int)n;
		if (n < 0 && errno != EINTR)
			return AVERROR(errno);
		/* the end of a recording that is still written: wait for it to grow,
		 * while it plays; a seek reads up to the end and goes on */
		if (!p->mGrowing || !p->mFollow || p->mAbort || p->mStop || p->mSeekReq || waited >= GROW_WAIT_MS)
			return AVERROR_EOF;
		usleep(50000);
		waited += 50;
	}
}

int64_t cDvbPlayer::ioSeek(void *opaque, int64_t offset, int whence)
{
	cDvbPlayer *p = (cDvbPlayer *)opaque;
	if (whence == AVSEEK_SIZE)
	{
		struct stat64 st;
		return fstat64(p->mFd, &st) ? -1 : st.st_size;
	}
	return lseek64(p->mFd, offset, whence & ~AVSEEK_FORCE);
}

int cDvbPlayer::openInput(const std::string &url)
{
	std::string path;
	if (url.compare(0, 7, "file://") == 0)
		path = url.substr(7);
	else if (!url.empty() && url[0] == '/')
		path = url;

	if (path.empty())
		return openStream(mFmt, url);

	mFmt = avformat_alloc_context();
	if (!mFmt)
		return AVERROR(ENOMEM);
	mFmt->interrupt_callback.callback = interruptCb;
	mFmt->interrupt_callback.opaque = this;
	{
		mFd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (mFd < 0)
			return AVERROR(errno);
		const int size = 64 * 1024;
		uint8_t *buf = (uint8_t *)av_malloc(size);
		mIo = avio_alloc_context(buf, size, 0, this, ioRead, NULL, ioSeek);
		if (!mIo)
			return AVERROR(ENOMEM);
		mFmt->pb = mIo;
		/* a recording just begun is short, do not wait for more to analyse */
		mFmt->probesize = 512 * 1024;
		mFmt->max_analyze_duration = AV_TIME_BASE / 2;
	}

	int ret = avformat_open_input(&mFmt, path.c_str(), NULL, NULL);
	if (ret < 0)
		return ret;
	return avformat_find_stream_info(mFmt, NULL);
}

/* a stream from the network, with the user agent and header fields of the start */
int cDvbPlayer::openStream(AVFormatContext *&fmt, const std::string &url)
{
	fmt = avformat_alloc_context();
	if (!fmt)
		return AVERROR(ENOMEM);
	fmt->interrupt_callback.callback = interruptCb;
	fmt->interrupt_callback.opaque = this;

	AVDictionary *opts = NULL;
	if (!mUserAgent.empty())
		av_dict_set(&opts, "user_agent", mUserAgent.c_str(), 0);
	std::string headers;
	for (size_t i = 0; i < mHeaderFields.size(); i++)
		headers += mHeaderFields[i] + "\r\n";
	if (!headers.empty())
		av_dict_set(&opts, "headers", headers.c_str(), 0);

	int ret = avformat_open_input(&fmt, url.c_str(), NULL, &opts);
	av_dict_free(&opts);
	if (ret < 0)
		return ret;
	return avformat_find_stream_info(fmt, NULL);
}

AVStream *cDvbPlayer::audioStream() const
{
	return (mAudioFmt ? mAudioFmt : mFmt)->streams[mAudioIdx];
}

/* a live stream has no end: its sound is opened anew */
bool cDvbPlayer::reopenSound()
{
	for (int tries = 0; tries < 5 && !mStop; tries++)
	{
		if (tries)
			usleep(1000000);
		AVFormatContext *fmt = NULL;
		int ret = openStream(fmt, mAudioUrl);
		int idx = ret >= 0 ? av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0) : ret;
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
	}
	return false;
}

bool cDvbPlayer::pickStreams()
{
	const char *name = mFmt->iformat ? mFmt->iformat->name : "";
	if (strcmp(name, "mpegts") && strcmp(name, "hls"))
	{
		hal_info("%s: container %s goes on to the fallback\n", __func__, name);
		return false;
	}

	mVideoIdx = av_find_best_stream(mFmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (mVideoIdx < 0)
		return false;
	enum AVCodecID vcodec = mFmt->streams[mVideoIdx]->codecpar->codec_id;
	mVideoType = streamTypeFor(vcodec);
	if (mVideoType < 0)
	{
		hal_info("%s: video %s goes on to the fallback\n", __func__, avcodec_get_name(vcodec));
		return false;
	}
	AVCodecParameters *vpar = mFmt->streams[mVideoIdx]->codecpar;
	if (vcodec == AV_CODEC_ID_H264 && H264_MAX_WIDTH && (vpar->width > H264_MAX_WIDTH || vpar->height > H264_MAX_HEIGHT))
	{
		hal_info("%s: h264 %dx%d goes on to the fallback\n", __func__, vpar->width, vpar->height);
		return false;
	}

	mAudioIdx = -1;
	if (mAudioFmt)
		mAudioIdx = av_find_best_stream(mAudioFmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	else
	{
		for (unsigned i = 0; i < mFmt->nb_streams; i++)
		{
			AVStream *st = mFmt->streams[i];
			if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && trackId(st) == mWantAudio)
				mAudioIdx = i;
		}
		if (mAudioIdx < 0)
			mAudioIdx = av_find_best_stream(mFmt, AVMEDIA_TYPE_AUDIO, -1, mVideoIdx, NULL, 0);
	}
	if (mAudioIdx >= 0)
	{
		enum AVCodecID acodec = audioStream()->codecpar->codec_id;
		mAudioType = bypassFor(acodec);
		if (mAudioType < 0)
		{
			hal_info("%s: audio %s goes on to the fallback\n", __func__, avcodec_get_name(acodec));
			return false;
		}
		mAudioId = trackId(audioStream());
	}
	for (unsigned i = 0; i < mFmt->nb_streams; i++)
		if ((int)i != mVideoIdx && (mAudioFmt || (int)i != mAudioIdx))
			mFmt->streams[i]->discard = AVDISCARD_ALL;
	for (unsigned i = 0; mAudioFmt && i < mAudioFmt->nb_streams; i++)
		if ((int)i != mAudioIdx)
			mAudioFmt->streams[i]->discard = AVDISCARD_ALL;

	mStart90 = mFmt->start_time != AV_NOPTS_VALUE ? av_rescale(mFmt->start_time, 90000, AV_TIME_BASE) : 0;
	hal_info("%s: %s, video %s, audio %s\n", __func__, name, avcodec_get_name(vcodec),
		 mAudioIdx >= 0 ? avcodec_get_name(audioStream()->codecpar->codec_id) : "none");
	return true;
}

void cDvbPlayer::setupDecoders()
{
	/* keep the live picture until the first one of the stream */
	videoDecoder->HoldPicture(true);
	videoDecoder->Stop(false);
	audioDecoder->Stop();
	mVideoFd = videoDecoder->fd;
	mAudioFd = audioDecoder->fd;

	AVStream *vst = mFmt->streams[mVideoIdx];
	int rate = fallbackRate(vst->avg_frame_rate.num > 0 ? vst->avg_frame_rate : vst->r_frame_rate);
	char old[16];
	mFallbackRate.clear();
	if (rate && proc_get(FALLBACK_RATE, old, sizeof(old)) > 0)
	{
		mFallbackRate = old;
		std::string val = std::to_string(rate);
		proc_put(FALLBACK_RATE, val.c_str(), val.size());
	}
	if (mVideoFd >= 0)
	{
		mVideoFlags = fcntl(mVideoFd, F_GETFL);
		fcntl(mVideoFd, F_SETFL, mVideoFlags | O_NONBLOCK);
		ioctl(mVideoFd, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_MEMORY);
		if (ioctl(mVideoFd, VIDEO_SET_STREAMTYPE, mVideoType) < 0 && mVideoType == STREAM_H265)
			ioctl(mVideoFd, VIDEO_SET_STREAMTYPE, STREAM_H265_ALT);
		ioctl(mVideoFd, VIDEO_PLAY);
		ioctl(mVideoFd, VIDEO_CONTINUE);
		ioctl(mVideoFd, VIDEO_CLEAR_BUFFER);
	}
	if (mAudioFd >= 0 && mAudioIdx >= 0)
	{
		mAudioFlags = fcntl(mAudioFd, F_GETFL);
		fcntl(mAudioFd, F_SETFL, mAudioFlags | O_NONBLOCK);
		ioctl(mAudioFd, AUDIO_CLEAR_BUFFER);
		ioctl(mAudioFd, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_MEMORY);
		if (ioctl(mAudioFd, AUDIO_SET_BYPASS_MODE, mAudioType) < 0 && mAudioType == BYPASS_EAC3)
			ioctl(mAudioFd, AUDIO_SET_BYPASS_MODE, BYPASS_EAC3_ALT);
		ioctl(mAudioFd, AUDIO_SET_AV_SYNC, 1);
		ioctl(mAudioFd, AUDIO_PLAY);
		ioctl(mAudioFd, AUDIO_CONTINUE);
	}
}

void cDvbPlayer::releaseDecoders()
{
	videoDecoder->HoldPicture(false);
	if (!mFallbackRate.empty())
		proc_put(FALLBACK_RATE, mFallbackRate.c_str(), mFallbackRate.size());
	mFallbackRate.clear();
	if (mVideoFd >= 0)
	{
		ioctl(mVideoFd, VIDEO_CLEAR_BUFFER);
		ioctl(mVideoFd, VIDEO_STOP, 0);
		ioctl(mVideoFd, VIDEO_SLOWMOTION, 0);
		ioctl(mVideoFd, VIDEO_FAST_FORWARD, 0);
		ioctl(mVideoFd, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_DEMUX);
		fcntl(mVideoFd, F_SETFL, mVideoFlags);
	}
	if (mAudioFd >= 0 && mAudioIdx >= 0)
	{
		ioctl(mAudioFd, AUDIO_CLEAR_BUFFER);
		ioctl(mAudioFd, AUDIO_STOP);
		ioctl(mAudioFd, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_DEMUX);
		fcntl(mAudioFd, F_SETFL, mAudioFlags);
	}
	mVideoFd = mAudioFd = -1;
}

int64_t cDvbPlayer::pts90(const AVPacket *pkt, const AVStream *st) const
{
	int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
	if (ts == AV_NOPTS_VALUE)
		return -1;
	return av_rescale_q(ts, st->time_base, AVRational{1, 90000}) & 0x1ffffffffLL;
}

bool cDvbPlayer::videoPes(AVPacket *pkt, std::vector<uint8_t> &pes)
{
	AVStream *st = mFmt->streams[mVideoIdx];
	int64_t pts = pts90(pkt, st);
	bool nal = mVideoType == STREAM_H264 || mVideoType == STREAM_H265;
	if (!nal)
	{
		pesHeader(pes, pkt->size, PES_VIDEO, pts, false);
		pes.insert(pes.end(), pkt->data, pkt->data + pkt->size);
		return true;
	}
	bool hevc = mVideoType == STREAM_H265;
	pesHeader(pes, -1, PES_VIDEO, pts, true);
	/* the parameter sets once in front, should the stream bring none */
	if (!mSpsSeen)
		mSpsSeen = hasParameterSet(pkt->data, pkt->size, hevc);
	AVCodecParameters *par = st->codecpar;
	if (!mSpsSeen && par->extradata_size > 4 && par->extradata[0] == 0)
		pes.insert(pes.end(), par->extradata, par->extradata + par->extradata_size);
	if (hevc)
		pes.push_back(0x00);
	pes.insert(pes.end(), pkt->data, pkt->data + pkt->size);
	return true;
}

bool cDvbPlayer::audioPes(AVPacket *pkt, std::vector<uint8_t> &pes)
{
	AVStream *st = audioStream();
	int64_t pts = pts90(pkt, st);
	AVCodecParameters *par = st->codecpar;
	bool adts = pkt->size > 1 && pkt->data[0] == 0xff && (pkt->data[1] & 0xf0) == 0xf0;
	if (par->codec_id == AV_CODEC_ID_AAC && !adts)
	{
		/* raw AAC, as HLS in fragmented MP4 carries it: give it an ADTS header */
		int len = pkt->size + 7;
		int profile = par->profile >= 0 && par->profile < 4 ? par->profile : 1;
		int rate = adtsRateIndex(par->sample_rate);
		int ch = par->ch_layout.nb_channels;
		pesHeader(pes, len, PES_AUDIO, pts, false);
		pes.push_back(0xff);
		pes.push_back(0xf1);
		pes.push_back((profile << 6) | (rate << 2) | ((ch >> 2) & 1));
		pes.push_back(((ch & 3) << 6) | (len >> 11));
		pes.push_back((len >> 3) & 0xff);
		pes.push_back(((len & 7) << 5) | 0x1f);
		pes.push_back(0xfc);
	}
	else
		pesHeader(pes, pkt->size, PES_AUDIO, pts, false);
	pes.insert(pes.end(), pkt->data, pkt->data + pkt->size);
	return true;
}

void cDvbPlayer::push(Queue &q, std::vector<uint8_t> &pes)
{
	std::unique_lock<std::mutex> lock(mQueueLock);
	mQueueCond.wait(lock, [&] { return q.bytes < q.limit || mStop || mSeekReq; });
	if (mStop || mSeekReq)
		return;
	q.bytes += pes.size();
	q.pes.push_back(std::move(pes));
	mQueueCond.notify_all();
}

void cDvbPlayer::writeLoop(int fd, Queue &q, unsigned &gen)
{
	std::unique_lock<std::mutex> lock(mQueueLock);
	while (!mStop)
	{
		if (q.pes.empty())
		{
			gen = mGen;
			mQueueCond.notify_all();
			mQueueCond.wait(lock);
			continue;
		}
		std::vector<uint8_t> pes = std::move(q.pes.front());
		q.pes.pop_front();
		q.bytes -= pes.size();
		unsigned g = mGen;
		mQueueCond.notify_all();
		lock.unlock();

		size_t off = 0;
		while (off < pes.size() && !mStop && g == mGen)
		{
			ssize_t n = write(fd, pes.data() + off, pes.size() - off);
			if (n > 0)
				off += n;
			else if (n < 0 && errno != EAGAIN && errno != EINTR)
				break;
			else if (n == 0 || errno == EAGAIN)
			{
				/* a short wait, so that a stop or flush is seen */
				struct pollfd pfd = { fd, POLLOUT, 0 };
				poll(&pfd, 1, 20);
			}
		}

		lock.lock();
		gen = g;
		mQueueCond.notify_all();
	}
}

void cDvbPlayer::flush()
{
	{
		std::lock_guard<std::mutex> lock(mQueueLock);
		mGen++;
		mVideoQ.pes.clear();
		mVideoQ.bytes = 0;
		mAudioQ.pes.clear();
		mAudioQ.bytes = 0;
		mQueueCond.notify_all();
	}
	/* a write the decoder holds back ends with its buffer cleared */
	if (mVideoFd >= 0)
		ioctl(mVideoFd, VIDEO_CLEAR_BUFFER);
	if (mAudioFd >= 0 && mAudioIdx >= 0)
		ioctl(mAudioFd, AUDIO_CLEAR_BUFFER);
	{
		std::unique_lock<std::mutex> lock(mQueueLock);
		mQueueCond.wait(lock, [&] { return mStop || (mVideoGen == mGen && (mAudioIdx < 0 || mAudioGen == mGen)); });
	}
	if (mVideoFd >= 0)
		ioctl(mVideoFd, VIDEO_CLEAR_BUFFER);
	if (mAudioFd >= 0 && mAudioIdx >= 0)
		ioctl(mAudioFd, AUDIO_CLEAR_BUFFER);
}

void cDvbPlayer::doSeek()
{
	int64_t target = mSeekTarget;
	mSeekReq = false;
	int64_t start = mFmt->start_time != AV_NOPTS_VALUE ? mFmt->start_time : 0;
	mFollow = false;
	int ret = av_seek_frame(mFmt, -1, start + target * 1000, AVSEEK_FLAG_BACKWARD);
	mFollow = true;
	hal_info("%s: to %lld ms%s\n", __func__, (long long)target, ret < 0 ? ", failed" : "");
	if (ret < 0)
		return;
	if (mAudioFmt)
		setAndWake(mAudioSeekReq);
	flush();
	mSpsSeen = false;
	mDemuxDone = false;
	mAudioFrom = -1;
	mEof = false;
}

/* the sound from a stream of its own, read beside the video */
void cDvbPlayer::audioDemuxLoop()
{
	AVPacket *pkt = av_packet_alloc();
	bool ended = false;
	while (!mStop)
	{
		if (mAudioSeekReq)
		{
			mAudioSeekReq = false;
			int64_t start = mFmt->start_time != AV_NOPTS_VALUE ? mFmt->start_time : 0;
			if (av_seek_frame(mAudioFmt, -1, start + mSeekTarget * 1000, AVSEEK_FLAG_BACKWARD) < 0)
				hal_info("%s: the sound does not seek\n", __func__);
			ended = false;
		}
		if (ended)
		{
			std::unique_lock<std::mutex> lock(mQueueLock);
			mQueueCond.wait_for(lock, std::chrono::milliseconds(200), [this] { return mStop || mAudioSeekReq; });
			continue;
		}
		int ret = av_read_frame(mAudioFmt, pkt);
		if (ret == AVERROR(EAGAIN))
		{
			usleep(10000);
			continue;
		}
		if (ret < 0)
		{
			if (!mStop && mFmt->duration <= 0 && reopenSound())
				continue;
			ended = true;
			continue;
		}
		std::vector<uint8_t> pes;
		if (pkt->stream_index == mAudioIdx && !mTrick && !mAudioSeekReq)
		{
			mBytes += pkt->size;
			if (audioPes(pkt, pes))
				push(mAudioQ, pes);
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
}

void cDvbPlayer::demuxLoop()
{
	AVPacket *pkt = av_packet_alloc();
	bool waitKey = true;
	uint64_t lastPts = 0;
	int stillMs = 0;
	while (!mStop)
	{
		if (mSeekReq)
		{
			doSeek();
			waitKey = true;
			continue;
		}
		if (mDemuxDone)
		{
			/* the end: when the queues are empty and the picture stands still */
			usleep(100000);
			bool empty;
			{
				std::lock_guard<std::mutex> lock(mQueueLock);
				empty = mVideoQ.pes.empty() && mAudioQ.pes.empty();
			}
			uint64_t now = pts();
			stillMs = (empty && now == lastPts && !mPaused) ? stillMs + 100 : 0;
			lastPts = now;
			if (stillMs >= 1000 && !mEof)
			{
				hal_info("%s: end of stream\n", __func__);
				mEof = true;
			}
			continue;
		}
		int ret = av_read_frame(mFmt, pkt);
		if (ret == AVERROR(EAGAIN))
		{
			usleep(10000);
			continue;
		}
		if (ret < 0)
		{
			if (!mStop && !mSeekReq)
			{
				hal_info("%s: read ends (%d)\n", __func__, ret);
				mDemuxDone = true;
			}
			continue;
		}
		mBytes += pkt->size;
		std::vector<uint8_t> pes;
		if (pkt->stream_index == mVideoIdx)
		{
			if (waitKey && !(pkt->flags & AV_PKT_FLAG_KEY))
			{
				av_packet_unref(pkt);
				continue;
			}
			waitKey = false;
			if (videoPes(pkt, pes))
				push(mVideoQ, pes);
		}
		else if (!mAudioFmt && pkt->stream_index == mAudioIdx && !waitKey && !mTrick)
		{
			/* the sound begins at the picture to go on from; the decoder
			 * keeps the picture in time with it and drops what is before */
			int64_t apts = pts90(pkt, audioStream());
			if (mAudioFrom >= 0 && apts >= 0 && ((mAudioFrom - apts) & 0x1ffffffffLL) < (1LL << 32))
			{
				av_packet_unref(pkt);
				continue;
			}
			mAudioFrom = -1;
			if (audioPes(pkt, pes))
				push(mAudioQ, pes);
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
}

uint64_t cDvbPlayer::decoderPts()
{
	int64_t pts = 0;
	if (mVideoFd >= 0 && ioctl(mVideoFd, VIDEO_GET_PTS, &pts) == 0 && pts > 0)
		return pts;
	if (mAudioFd >= 0 && mAudioIdx >= 0 && ioctl(mAudioFd, AUDIO_GET_PTS, &pts) == 0 && pts > 0)
		return pts;
	return 0;
}

uint64_t cDvbPlayer::pts()
{
	uint64_t pts = decoderPts();
	return pts ? (pts - mStart90) & 0x1ffffffffLL : 0;
}

/* the decoder keeps the time of the live picture until it shows one of the
 * stream; going on from a held picture, until it has reached that */
bool cDvbPlayer::waitFirstPicture(uint64_t livePts, int64_t from)
{
	for (int i = 0; i < 250 && !mStop && !mAbort; i++)
	{
		uint64_t pts = decoderPts();
		if (pts && pts != livePts && (from < 0 || ((pts - from) & 0x1ffffffffLL) < (1LL << 32)))
			return true;
		usleep(20000);
	}
	return false;
}

cHwPlayer::Result cDvbPlayer::start(const std::string &url, const std::string &audioUrl, const std::string &userAgent,
				    const std::vector<std::string> &headerFields, int &error, int startMs, bool growing)
{
	stop();
	mStop = false;
	mEof = false;
	mPaused = false;
	mTrick = false;
	mSeekReq = false;
	mDemuxDone = false;
	mSpsSeen = false;
	mBytes = 0;
	mGrowing = growing;
	mFollow = false;
	mAudioFrom = -1;
	mAudioSeekReq = false;
	mUserAgent = userAgent;
	mHeaderFields = headerFields;
	mAudioUrl = growing ? "" : audioUrl;
	error = 0;

	int ret = openInput(url);
	/* the sound as a stream of its own, as YouTube has it */
	if (ret >= 0 && !mAudioUrl.empty())
		ret = openStream(mAudioFmt, mAudioUrl);
	if (mAbort)
	{
		stop();
		return ABORTED;
	}
	if (ret < 0)
	{
		error = ret;
		hal_info("%s: cannot open %s (%d)\n", __func__, url.c_str(), ret);
		stop();
		return FAILED;
	}
	if (!pickStreams())
	{
		stop();
		return UNSUPPORTED;
	}
	/* a timeshift of held live TV goes on from the picture held, which the
	 * recording carries with the same time */
	int64_t held = growing ? videoDecoder->HeldPts() : -1;
	AVStream *vst = mFmt->streams[mVideoIdx];
	if (held > 0)
	{
		int64_t ts = av_rescale_q(held, AVRational{1, 90000}, vst->time_base);
		int64_t wrap = av_rescale_q(1LL << 33, AVRational{1, 90000}, vst->time_base);
		if (vst->start_time != AV_NOPTS_VALUE && ts < vst->start_time)
			ts += wrap;
		hal_info("%s: from the held picture at %lld\n", __func__, (long long)held);
		/* a stream that refreshes its picture over a while, without real
		 * I-frames, is complete only some time after its key frame */
		int64_t lead = av_rescale_q(HELD_LEAD_MS * 90LL, AVRational{1, 90000}, vst->time_base);
		av_seek_frame(mFmt, mVideoIdx, ts - lead, AVSEEK_FLAG_BACKWARD);
		mAudioFrom = held;
	}
	else if (startMs > 0)
	{
		int64_t start = mFmt->start_time != AV_NOPTS_VALUE ? mFmt->start_time : 0;
		bool exact = !mAudioFmt && mAudioIdx >= 0;
		int64_t from = exact ? std::max<int64_t>(startMs - HELD_LEAD_MS, 0) : startMs;
		av_seek_frame(mFmt, -1, start + from * 1000, AVSEEK_FLAG_BACKWARD);
		if (mAudioFmt)
			av_seek_frame(mAudioFmt, -1, start + (int64_t)startMs * 1000, AVSEEK_FLAG_BACKWARD);
		else if (exact)
			mAudioFrom = (mStart90 + (int64_t)startMs * 90) & 0x1ffffffffLL;
	}

	uint64_t livePts = videoDecoder->GetPTS();
	int64_t from = mAudioFrom;
	setupDecoders();
	mVideoGen = mAudioGen = mGen;
	mFollow = true;
	mDemuxThread = std::thread(&cDvbPlayer::demuxLoop, this);
	if (mAudioFmt && mAudioIdx >= 0)
		mAudioDemuxThread = std::thread(&cDvbPlayer::audioDemuxLoop, this);
	mVideoThread = std::thread(&cDvbPlayer::writeLoop, this, mVideoFd, std::ref(mVideoQ), std::ref(mVideoGen));
	if (mAudioIdx >= 0)
		mAudioThread = std::thread(&cDvbPlayer::writeLoop, this, mAudioFd, std::ref(mAudioQ), std::ref(mAudioGen));
	mPlaying = true;

	if (!waitFirstPicture(livePts, from))
		hal_info("%s: no picture yet\n", __func__);
	videoDecoder->HoldPicture(false);
	if (mAbort)
	{
		stop();
		return ABORTED;
	}
	return OK;
}

void cDvbPlayer::stop()
{
	setAndWake(mStop);
	if (mVideoFd >= 0)
		ioctl(mVideoFd, VIDEO_CLEAR_BUFFER);
	if (mAudioFd >= 0 && mAudioIdx >= 0)
		ioctl(mAudioFd, AUDIO_CLEAR_BUFFER);
	if (mDemuxThread.joinable())
		mDemuxThread.join();
	if (mAudioDemuxThread.joinable())
		mAudioDemuxThread.join();
	if (mVideoThread.joinable())
		mVideoThread.join();
	if (mAudioThread.joinable())
		mAudioThread.join();
	if (mPlaying)
		releaseDecoders();
	mPlaying = false;

	mVideoQ.pes.clear();
	mVideoQ.bytes = 0;
	mAudioQ.pes.clear();
	mAudioQ.bytes = 0;
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
	mVideoIdx = mAudioIdx = -1;
}

bool cDvbPlayer::setPaused(bool paused)
{
	if (!mPlaying)
		return false;
	if (paused)
	{
		ioctl(mVideoFd, VIDEO_FREEZE);
		if (mAudioIdx >= 0)
			ioctl(mAudioFd, AUDIO_PAUSE);
	}
	else
	{
		ioctl(mVideoFd, VIDEO_FAST_FORWARD, 0);
		ioctl(mVideoFd, VIDEO_CONTINUE);
		if (mAudioIdx >= 0)
			ioctl(mAudioFd, AUDIO_CONTINUE);
	}
	mPaused = paused;
	return true;
}

bool cDvbPlayer::seek(int64_t ms, bool absolute)
{
	if (!mPlaying)
		return false;
	if (!absolute)
	{
		int64_t pos = 0, dur = 0;
		position(pos, dur);
		ms += pos;
	}
	if (ms < 0)
		ms = 0;
	mSeekTarget = ms;
	setAndWake(mSeekReq);
	return true;
}

void cDvbPlayer::setTrick(bool on)
{
	if (mTrick == on || !mPlaying)
		return;
	mTrick = on;
	if (mAudioIdx >= 0)
		ioctl(mAudioFd, AUDIO_SET_AV_SYNC, on ? 0 : 1);
}

bool cDvbPlayer::position(int64_t &posMs, int64_t &durMs)
{
	if (!mPlaying)
		return false;
	posMs = pts() / 90;
	durMs = mFmt && mFmt->duration != AV_NOPTS_VALUE ? mFmt->duration / 1000 : 0;
	return true;
}

void cDvbPlayer::audioTracks(std::vector<Track> &tracks)
{
	tracks.clear();
	AVFormatContext *fmt = mAudioFmt ? mAudioFmt : mFmt;
	if (!fmt)
		return;
	for (unsigned i = 0; i < fmt->nb_streams; i++)
	{
		AVStream *st = fmt->streams[i];
		if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO || bypassFor(st->codecpar->codec_id) < 0)
			continue;
		Track t;
		t.id = trackId(st);
		t.ac3flag = ac3flag(st->codecpar->codec_id);
		AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL, 0);
		t.language = lang ? lang->value : "Stream " + std::to_string(t.id);
		tracks.push_back(t);
	}
}

void cDvbPlayer::metadata(std::vector<std::string> &keys, std::vector<std::string> &values)
{
	keys.clear();
	values.clear();
	if (!mFmt)
		return;
	AVDictionaryEntry *e = NULL;
	while ((e = av_dict_get(mFmt->metadata, "", e, AV_DICT_IGNORE_SUFFIX)))
	{
		keys.push_back(e->key);
		values.push_back(e->value);
	}
}
