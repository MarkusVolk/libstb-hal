/*
 * A decoder of the platform's own in front of mpv
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#ifndef __HW_PLAYER_H__
#define __HW_PLAYER_H__

#include <cstdint>
#include <string>
#include <vector>

class cHwPlayer
{
	public:
		enum Result { OK, UNSUPPORTED, FAILED, ABORTED };

		struct Track
		{
			int id;
			int ac3flag;
			std::string language;
		};

		virtual ~cHwPlayer() {}

		/* before every start(): forget an earlier abort */
		virtual void reset() = 0;
		/* blocks until the stream plays, or until it is clear the decoder
		 * cannot take it (UNSUPPORTED, mpv plays it then); audioUrl: the
		 * sound from a stream of its own, startMs: where in a file to
		 * begin, growing: a recording that is still written */
		virtual Result start(const std::string &url, const std::string &audioUrl, const std::string &userAgent,
				     const std::vector<std::string> &headerFields, int &error, int startMs, bool growing) = 0;
		virtual void stop() = 0;
		/* from another thread: ends a start() that still opens */
		virtual void abort() = 0;

		virtual bool isPlaying() const = 0;
		virtual bool eof() const = 0;
		virtual bool setPaused(bool paused) = 0;
		/* in ms from the start of the stream, or from where it plays */
		virtual bool seek(int64_t ms, bool absolute) = 0;
		/* while spooling: the sound off, the picture at normal rate */
		virtual void setTrick(bool on) = 0;
		virtual bool position(int64_t &posMs, int64_t &durMs) = 0;
		virtual uint64_t readCount() = 0;
		virtual int bufferedMs() = 0;
		virtual void audioTracks(std::vector<Track> &tracks) = 0;
		virtual int audioTrack() const = 0;
		virtual void metadata(std::vector<std::string> &keys, std::vector<std::string> &values) = 0;
};

#endif
