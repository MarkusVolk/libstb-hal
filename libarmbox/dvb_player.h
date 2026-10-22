/*
 * Playback through the box's own decoders: ffmpeg demuxes, the
 * elementary streams go as PES into the video and audio devices
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#ifndef __DVB_PLAYER_H__
#define __DVB_PLAYER_H__

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "hw_player.h"

struct AVFormatContext;
struct AVIOContext;
struct AVPacket;
struct AVStream;

class cDvbPlayer : public cHwPlayer
{
	public:
		cDvbPlayer();
		~cDvbPlayer() override;

		void reset() override;
		Result start(const std::string &url, const std::string &audioUrl, const std::string &userAgent,
			     const std::vector<std::string> &headerFields, int &error, int startMs, bool growing) override;
		void stop() override;
		void abort() override;

		bool isPlaying() const override { return mPlaying; }
		bool eof() const override { return mEof; }
		bool setPaused(bool paused) override;
		bool seek(int64_t ms, bool absolute) override;
		void setTrick(bool on) override;
		bool position(int64_t &posMs, int64_t &durMs) override;
		uint64_t readCount() override { return mBytes; }
		int bufferedMs() override { return 0; }
		void audioTracks(std::vector<Track> &tracks) override;
		int audioTrack() const override { return mAudioId; }
		void metadata(std::vector<std::string> &keys, std::vector<std::string> &values) override;

		/* the audio track to take when the stream has several */
		void wantAudio(int id) { mWantAudio = id; }
		/* 90 kHz, of the picture shown */
		uint64_t pts();

	private:
		struct Queue
		{
			std::deque<std::vector<uint8_t> > pes;
			size_t bytes = 0;
			size_t limit = 0;
		};

		static int interruptCb(void *opaque);
		static int ioRead(void *opaque, uint8_t *buf, int size);
		static int64_t ioSeek(void *opaque, int64_t offset, int whence);

		int openInput(const std::string &url, const std::string &userAgent, const std::vector<std::string> &headerFields);
		bool pickStreams();
		void setupDecoders();
		void releaseDecoders();
		void demuxLoop();
		void writeLoop(int fd, Queue &q, unsigned &gen);
		void doSeek();
		void flush();
		bool videoPes(AVPacket *pkt, std::vector<uint8_t> &pes);
		bool audioPes(AVPacket *pkt, std::vector<uint8_t> &pes);
		int64_t pts90(const AVPacket *pkt, const AVStream *st) const;
		void push(Queue &q, std::vector<uint8_t> &pes);
		uint64_t decoderPts();
		bool waitFirstPicture(uint64_t livePts);

		std::atomic<bool> mAbort;
		std::atomic<bool> mStop;
		std::atomic<bool> mPlaying;
		std::atomic<bool> mEof;
		std::atomic<bool> mPaused;
		std::atomic<bool> mTrick;
		std::atomic<uint64_t> mBytes;
		std::atomic<bool> mSeekReq;
		std::atomic<int64_t> mSeekTarget; /* ms, absolute */
		bool mGrowing;
		std::atomic<bool> mFollow;       /* wait at the end of a growing recording */
		bool mDemuxDone;
		int mFd;                          /* a local file, read through mIo */
		AVIOContext *mIo;
		AVFormatContext *mFmt;
		int mVideoIdx;
		int mAudioIdx;
		int mAudioId;
		int mWantAudio;
		int mVideoType;                   /* VIDEO_SET_STREAMTYPE */
		int mAudioType;                   /* AUDIO_SET_BYPASS_MODE */
		bool mAdts;
		bool mSpsSeen;
		int64_t mStart90;                 /* the start of the stream, the decoder gets its own times */
		int mVideoFd;
		int mAudioFd;
		int mVideoFlags;
		int mAudioFlags;

		std::mutex mQueueLock;
		std::condition_variable mQueueCond;
		Queue mVideoQ;
		Queue mAudioQ;
		unsigned mGen;                    /* bumped by a flush */
		unsigned mVideoGen;
		unsigned mAudioGen;

		std::thread mDemuxThread;
		std::thread mVideoThread;
		std::thread mAudioThread;
};

#endif
