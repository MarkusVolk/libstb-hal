/*
 * The decoder devices as the sink of cDvbPlayer
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#include <config.h>

#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/dvb/audio.h>
#include <linux/dvb/video.h>

#include "dvb_sink.h"
#include "audio_lib.h"
#include "video_lib.h"
#include "proc_tools.h"

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
