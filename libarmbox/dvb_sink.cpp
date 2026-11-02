/*
 * The decoder devices as the sink of cDvbPlayer
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#include <config.h>

#include <algorithm>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include <linux/dvb/audio.h>
#include <linux/dvb/video.h>

#include "dvb_sink.h"
#include "audio_lib.h"
#include "video_lib.h"
#include "proc_tools.h"
#include "hisi_live.h"

#ifndef AUDIO_GET_PTS
#define AUDIO_GET_PTS _IOR('o', 19, __u64)
#endif

extern cAudio *audioDecoder;
extern cVideo *videoDecoder;

namespace
{

/* the H.265 stream type and E-AC3 bypass of older drivers */
const int STREAM_H265_ALT = 22;
const int BYPASS_EAC3_ALT = 7;
const char *FALLBACK_RATE = "/proc/stb/vmpeg/0/fallback_framerate";

} /* namespace */

class cDeviceSink : public cDvbSink
{
	public:
		bool open(int videoType, int audioType, bool sound, int rate) override;
		void close() override;
		ssize_t write(bool video, const uint8_t *data, size_t len) override
		{
			return ::write(video ? mVideoFd : mAudioFd, data, len);
		}
		void wait(bool video, int ms) override
		{
			struct pollfd pfd = { video ? mVideoFd : mAudioFd, POLLOUT, 0 };
			poll(&pfd, 1, ms);
		}
		void clear() override;
		void pause(bool on) override;
		void trick(bool on) override
		{
			if (mSound)
				ioctl(mAudioFd, AUDIO_SET_AV_SYNC, on ? 0 : 1);
		}
		uint64_t pts() override;

	private:
		int mVideoFd = -1, mAudioFd = -1;
		int mVideoFlags = 0, mAudioFlags = 0;
		bool mSound = false;
		std::string mFallbackRate;	/* the decoder's own, back at the end */
};

bool cDeviceSink::open(int videoType, int audioType, bool sound, int rate)
{
	mVideoFd = videoDecoder->fd;
	mAudioFd = audioDecoder->fd;
	mSound = sound && mAudioFd >= 0;
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
		if (ioctl(mVideoFd, VIDEO_SET_STREAMTYPE, videoType) < 0 && videoType == STREAM_H265)
			ioctl(mVideoFd, VIDEO_SET_STREAMTYPE, STREAM_H265_ALT);
		ioctl(mVideoFd, VIDEO_PLAY);
		ioctl(mVideoFd, VIDEO_CONTINUE);
		ioctl(mVideoFd, VIDEO_CLEAR_BUFFER);
	}
	if (mSound)
	{
		mAudioFlags = fcntl(mAudioFd, F_GETFL);
		fcntl(mAudioFd, F_SETFL, mAudioFlags | O_NONBLOCK);
		ioctl(mAudioFd, AUDIO_CLEAR_BUFFER);
		ioctl(mAudioFd, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_MEMORY);
		if (ioctl(mAudioFd, AUDIO_SET_BYPASS_MODE, audioType) < 0 && audioType == BYPASS_EAC3)
			ioctl(mAudioFd, AUDIO_SET_BYPASS_MODE, BYPASS_EAC3_ALT);
		ioctl(mAudioFd, AUDIO_SET_AV_SYNC, 1);
		ioctl(mAudioFd, AUDIO_PLAY);
		ioctl(mAudioFd, AUDIO_CONTINUE);
	}
	return true;
}

void cDeviceSink::close()
{
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
	if (mSound)
	{
		ioctl(mAudioFd, AUDIO_CLEAR_BUFFER);
		ioctl(mAudioFd, AUDIO_STOP);
		ioctl(mAudioFd, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_DEMUX);
		fcntl(mAudioFd, F_SETFL, mAudioFlags);
	}
	mVideoFd = mAudioFd = -1;
	mSound = false;
}

void cDeviceSink::clear()
{
	if (mVideoFd >= 0)
		ioctl(mVideoFd, VIDEO_CLEAR_BUFFER);
	if (mSound)
		ioctl(mAudioFd, AUDIO_CLEAR_BUFFER);
}

void cDeviceSink::pause(bool on)
{
	if (on)
	{
		ioctl(mVideoFd, VIDEO_FREEZE);
		if (mSound)
			ioctl(mAudioFd, AUDIO_PAUSE);
	}
	else
	{
		ioctl(mVideoFd, VIDEO_FAST_FORWARD, 0);
		ioctl(mVideoFd, VIDEO_CONTINUE);
		if (mSound)
			ioctl(mAudioFd, AUDIO_CONTINUE);
	}
}

uint64_t cDeviceSink::pts()
{
	int64_t pts = 0;
	if (mVideoFd >= 0 && ioctl(mVideoFd, VIDEO_GET_PTS, &pts) == 0 && pts > 0)
		return pts;
	if (mSound && ioctl(mAudioFd, AUDIO_GET_PTS, &pts) == 0 && pts > 0)
		return pts;
	return 0;
}

cDvbSink *newDeviceSink()
{
	return new cDeviceSink();
}

namespace
{

/* zapit's audio types and libstb-hal's video formats, as cHisiLive takes them */
enum { HISI_AUDIO_AC3 = 0, HISI_AUDIO_MPEG = 1, HISI_AUDIO_AAC = 8, HISI_AUDIO_EAC3 = 0x22, HISI_AUDIO_MP3 = 0x100 };
enum { HISI_FORMAT_MPEG2 = 0, HISI_FORMAT_H264 = 1, HISI_FORMAT_H265 = 6 };

const size_t TS_SIZE = 188;
const size_t TS_PAYLOAD = 184;
/* the most of a PES that goes into the buffer at once */
const size_t CHUNK_PACKETS = 256;
/* how far, 90 kHz, the stream goes ahead of the picture shown, or before
 * one is shown of its first time: the demux hands on what it gets, and
 * the AVPLAY's buffers drop what is too much */
const int64_t LEAD = 2 * 90000;
const int64_t FIRST_LEAD = 3 * 90000;

/* the PTS of a PES header, -1 without */
int64_t pesPts(const uint8_t *d, size_t len)
{
	if (len < 14 || d[0] || d[1] || d[2] != 1 || !(d[7] & 0x80))
		return -1;
	return ((int64_t)(d[9] & 0x0e) << 29) | (d[10] << 22) | ((d[11] & 0xfe) << 14) | (d[12] << 7) | (d[13] >> 1);
}

/* b - a across the 33 bit wrap */
int64_t ptsDiff(int64_t b, int64_t a)
{
	int64_t d = (b - a) & 0x1ffffffffLL;
	return d >= (1LL << 32) ? d - (1LL << 33) : d;
}

/* the PES as a transport stream on the PIDs of cHisiLive's playback */
class cHisiSink : public cDvbSink
{
	public:
		cHisiSink(cHisiLive *live) : mLive(live) {}
		bool open(int videoType, int audioType, bool sound, int rate) override;
		void close() override { mLive->StopPlayback(); }
		ssize_t write(bool video, const uint8_t *data, size_t len) override;
		void wait(bool, int ms) override { usleep(ms * 1000); }
		void clear() override
		{
			std::lock_guard<std::mutex> lock(mLock);
			mAtStart[0] = mAtStart[1] = true;
			mFirst[0] = mFirst[1] = -1;
			mLive->ClearPlayback();
		}
		void pause(bool on) override { mLive->PausePlayback(on); }
		void trick(bool on) override { mLive->TrickPlayback(on); }
		uint64_t pts() override { return mLive->GetPts(); }
		bool fakeStartCode() const override { return false; }

	private:
		cHisiLive *mLive;
		std::mutex mLock;
		/* per stream, video 0 and sound 1: the continuity counter, and
		 * whether the next write begins a PES */
		uint8_t mCc[2] = { 0, 0 };
		bool mAtStart[2] = { true, true };
		/* the first PTS since the start or a flush, -1 none yet */
		int64_t mFirst[2] = { -1, -1 };
		std::vector<uint8_t> mTs;
};

bool cHisiSink::open(int videoType, int audioType, bool sound, int)
{
	int format;
	switch (videoType)
	{
		case STREAM_H264: format = HISI_FORMAT_H264; break;
		case STREAM_H265: format = HISI_FORMAT_H265; break;
		default: format = HISI_FORMAT_MPEG2; break;
	}
	int audio;
	switch (audioType)
	{
		case BYPASS_AC3: audio = HISI_AUDIO_AC3; break;
		case BYPASS_EAC3: audio = HISI_AUDIO_EAC3; break;
		case BYPASS_MP3: audio = HISI_AUDIO_MP3; break;
		case BYPASS_AAC_ADTS: audio = HISI_AUDIO_AAC; break;
		default: audio = HISI_AUDIO_MPEG; break;
	}
	std::lock_guard<std::mutex> lock(mLock);
	mCc[0] = mCc[1] = 0;
	mAtStart[0] = mAtStart[1] = true;
	mFirst[0] = mFirst[1] = -1;
	return mLive->StartPlayback(format, audio, sound);
}

/* a PES is written from its start to its end, maybe in several writes:
 * whole TS payloads, the last one filled up by its adaptation field */
ssize_t cHisiSink::write(bool video, const uint8_t *data, size_t len)
{
	std::lock_guard<std::mutex> lock(mLock);
	int s = video ? 0 : 1;
	unsigned pid = video ? 0x100 : 0x101;
	int64_t pts = mAtStart[s] ? pesPts(data, len) : -1;
	if (pts >= 0)
	{
		int64_t shown = mLive->GetPts();
		if (mFirst[s] < 0)
			mFirst[s] = pts;
		bool ahead = shown > 0 ? ptsDiff(pts, shown & 0x1ffffffffLL) > LEAD : ptsDiff(pts, mFirst[s]) > FIRST_LEAD;
		if (ahead)
		{
			errno = EAGAIN;
			return 0;
		}
	}
	size_t take = std::min(len, CHUNK_PACKETS * TS_PAYLOAD);
	if (take < len)
		take -= take % TS_PAYLOAD;
	size_t packets = (take + TS_PAYLOAD - 1) / TS_PAYLOAD;
	mTs.resize(packets * TS_SIZE);
	uint8_t cc = mCc[s];
	const uint8_t *in = data;
	size_t left = take;
	for (size_t i = 0; i < packets; i++)
	{
		uint8_t *t = &mTs[i * TS_SIZE];
		size_t n = std::min(left, TS_PAYLOAD);
		bool start = i == 0 && mAtStart[s];
		t[0] = 0x47;
		t[1] = (start ? 0x40 : 0x00) | (pid >> 8);
		t[2] = pid & 0xff;
		size_t head = 4;
		if (n < TS_PAYLOAD)
		{
			size_t af = TS_PAYLOAD - n - 1;
			t[3] = 0x30 | cc;
			t[4] = af;
			if (af)
			{
				t[5] = 0x00;
				memset(t + 6, 0xff, af - 1);
			}
			head = 5 + af;
		}
		else
			t[3] = 0x10 | cc;
		memcpy(t + head, in, n);
		in += n;
		left -= n;
		cc = (cc + 1) & 0x0f;
	}
	if (!mLive->Feed(mTs.data(), mTs.size()))
	{
		errno = EAGAIN;
		return 0;
	}
	mCc[s] = cc;
	mAtStart[s] = take == len;
	return take;
}

} /* namespace */

cDvbSink *newHisiSink()
{
	if (cHisiLive *h = cHisiLive::getInstance())
		return new cHisiSink(h);
	return NULL;
}
