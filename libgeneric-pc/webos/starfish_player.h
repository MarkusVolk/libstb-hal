/*
 * Playback through the media pipeline of an LG webOS TV
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#ifndef __STARFISH_PLAYER_H__
#define __STARFISH_PLAYER_H__

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "hw_player.h"

struct AVFormatContext;
struct AVIOContext;
struct AVPacket;
class StarfishMediaAPIs;

/* ffmpeg demuxes, the TV decodes: the elementary streams go into one
 * StarfishMediaAPIs instance, which keeps audio and video in sync and
 * puts the picture on the video plane below the exported window */
class cStarfishPlayer : public cHwPlayer
{
	public:
		cStarfishPlayer();
		~cStarfishPlayer() override;

		void reset() override;
		Result start(const std::string &url, const std::string &userAgent, const std::vector<std::string> &headerFields, int &error,
			     int startMs, bool growing) override;
		void stop() override;
		void abort() override;

		bool isPlaying() const override { return mLoaded; }
		bool eof() const override { return mEof; }
		bool setPaused(bool paused) override;
		bool seek(int64_t ms, bool absolute) override;
		void setTrick(bool on) override;
		bool position(int64_t &posMs, int64_t &durMs) override;
		uint64_t readCount() override;
		int bufferedMs() override;
		void audioTracks(std::vector<Track> &tracks) override;
		int audioTrack() const override { return mAudioIdx; }
		void metadata(std::vector<std::string> &keys, std::vector<std::string> &values) override;

	private:
		struct Queue
		{
			std::deque<AVPacket *> packets;
			size_t bytes = 0;
			bool done = false;
			bool eosSent = false;
		};

		static int interruptCb(void *opaque);
		static int ioRead(void *opaque, uint8_t *buf, int size);
		static int64_t ioSeek(void *opaque, int64_t offset, int whence);
		void doSeek();
		static void eventCb(int type, int64_t numValue, const char *strValue, void *data);
		void event(int type, int64_t numValue, const char *strValue);

		int bestVideo();
		void normalRate();
		bool supported(std::string &videoCodec, std::string &audioCodec);
		bool preroll(int &error);
		std::string loadPayload(const std::string &videoCodec, const std::string &audioCodec);
		int64_t packetPts(const AVPacket *pkt) const;

		void demuxLoop();
		void feedLoop(int esData);
		bool feed(AVPacket *pkt, int esData);
		void push(Queue &q, AVPacket *pkt);
		void closeQueues();
		void emptyQueues();

		std::atomic<bool> mAbort;
		std::atomic<bool> mStop;
		std::atomic<bool> mLoaded;
		std::atomic<bool> mEof;
		std::atomic<int64_t> mFramePts; /* ns, from FRAMEREADY */
		std::atomic<int64_t> mDemuxPts; /* ns, last packet read */
		std::atomic<uint64_t> mBytes;
		int64_t mStartPts;              /* ns, first video keyframe */
		int64_t mFileStart;             /* ns, start of the stream */
		bool mTimeSet;
		std::atomic<bool> mSeekReq;
		std::atomic<int64_t> mSeekTarget; /* ns, absolute */
		std::atomic<bool> mFlushing;
		bool mWaitKey;
		std::mutex mFeedLock[3];        /* per esData: a packet is being fed */
		int mFd;                        /* a growing recording */
		AVIOContext *mIo;

		AVFormatContext *mFmt;
		int mVideoIdx;
		int mAudioIdx;
		bool mAdts;

		StarfishMediaAPIs *mApi;
		std::mutex mEventLock;
		std::condition_variable mEventCond;
		bool mLoadDone;
		bool mUnloadDone;

		std::mutex mQueueLock;
		std::condition_variable mQueueCond;
		Queue mVideoQ;
		Queue mAudioQ;
		std::vector<AVPacket *> mPreroll;

		std::thread mDemuxThread;
		std::thread mVideoThread;
		std::thread mAudioThread;
};

#endif
