/*
 * The picture in picture: a second live picture, decoded here without
 * sound and shown by the GL thread.
 *
 * License: GPL
 */

#ifndef __PIP_H__
#define __PIP_H__

#include <deque>
#include <stdint.h>

#include <OpenThreads/Thread>
#include <OpenThreads/Mutex>
#include <OpenThreads/ScopedLock>
#include <OpenThreads/Condition>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "video_lib.h"

AVCodecID hal_codec_for_format(VIDEO_FORMAT format);
void hal_unit_pids(int unit, uint16_t &vpid, uint16_t &apid, uint16_t &pcrpid);

class cPip : public OpenThreads::Thread
{
	public:
		static cPip *getInstance();
		/* the decoder reads the video PID zapit set on the demux unit */
		bool start(int unit, int vtype);
		void stop();
		/* where the picture goes, in the OSD's coordinates */
		void setRect(int x, int y, int w, int h, int osd_w, int osd_h);
		void show(bool on);
		/* started, shown and a picture decoded */
		bool showing();

		/* for the GL thread: the window as a share of the screen */
		struct Window
		{
			bool on;
			double x, y, w, h;
		};
		Window window();
		/* the newest frame that is due, NULL if none; the caller frees it */
		AVFrame *takeFrame();
		/* frames are waiting */
		bool pending();
		/* called when there is something new to show */
		void setWake(void (*wake)(void *), void *ctx);

	private:
		cPip();
		void run();
		bool openDecoder();
		void closeDecoder();
		void decode(const uint8_t *data, int size, int64_t pts);
		void push(AVFrame *f);
		void packet(const uint8_t *ts);
		void wake();
		void (*mWake)(void *);
		void *mWakeCtx;

		OpenThreads::Mutex mLock;
		OpenThreads::Condition mTaken; /* the GL thread took a frame or the end came */
		bool mRunning;
		volatile bool mQuit;
		int mCancel[2];
		int mUnit;
		int mVpid;
		int mVtype;
		bool mShow;
		bool mDecoded;
		Window mWin;

		/* the reader's state */
		AVCodecContext *mCtx;
		AVCodecParserContext *mParser;
		AVBufferRef *mHw;
		bool mPesOpen;
		int64_t mPesPts;
		int mBadFrames;

		/* decoded frames, oldest first, and when they are shown */
		std::deque<AVFrame *> mFrames;
		int64_t mBase;		/* monotonic microseconds minus PTS of the clock */
};

#endif
