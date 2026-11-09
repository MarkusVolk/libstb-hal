/*
 * Where the PES of cDvbPlayer goes: the decoder devices
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#ifndef __DVB_SINK_H__
#define __DVB_SINK_H__
#include <stdint.h>
#include <sys/types.h>

/* the codecs, as VIDEO_SET_STREAMTYPE and AUDIO_SET_BYPASS_MODE name them */
enum
{
	STREAM_MPEG2 = 0,
	STREAM_H264 = 1,
	STREAM_MPEG1 = 6,
	STREAM_H265 = 7,
	BYPASS_AC3 = 0,
	BYPASS_MPEG = 1,
	BYPASS_MP3 = 0xa,
	BYPASS_AAC_ADTS = 0xb,
	BYPASS_EAC3 = 0x22
};

class cDvbSink
{
	public:
		virtual ~cDvbSink() {}
		/* the decoder's stream and bypass types of cDvbPlayer, sound false
		 * without a sound track; rate in frames per second, 0 unknown */
		virtual bool open(int videoType, int audioType, bool sound, int rate) = 0;
		virtual void close() = 0;
		/* like write(2): the bytes taken, 0 while full, -1 with errno */
		virtual ssize_t write(bool video, const uint8_t *data, size_t len) = 0;
		/* waits up to ms for room */
		virtual void wait(bool video, int ms) = 0;
		/* drops what the decoders have not shown or played yet */
		virtual void clear() = 0;
		virtual void pause(bool on) = 0;
		/* fast forward and rewind: the pictures alone, without sync */
		virtual void trick(bool on) = 0;
		/* 90 kHz, of the picture shown, else of the sound; 0 unknown */
		virtual uint64_t pts() = 0;
		/* the start code behind the PES header that Broadcom's decoder looks for */
		virtual bool fakeStartCode() const { return true; }
};

/* the video and audio devices of the live decoders */
cDvbSink *newDeviceSink();

#endif
