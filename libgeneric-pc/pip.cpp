/*
 * The picture in picture: the demux unit zapit tuned for it is read as a
 * transport stream, the video PID unpacked and decoded by libavcodec with
 * the hardware the platform has. The frames are handed to the GL thread
 * at the time their PTS says.
 *
 * License: GPL
 */

#include <config.h>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

#include "pip.h"
#include "dmx_hal.h"
#include "hal_debug.h"

#define hal_debug(args...) _hal_debug(HAL_DEBUG_VIDEO, this, args)
#define hal_info(args...) _hal_info(HAL_DEBUG_VIDEO, this, args)

#define TS_SIZE 188
#define PIP_DMX_BUFFER (2 * 1024 * 1024)
/* decoded frames waiting for their time; the decoder waits beyond that,
 * and what comes in meanwhile waits in the demux, still compressed */
#define PIP_QUEUE 6
/* how far the picture runs behind the stream: a broadcast sends a large
 * picture early and over a long time, its decoding may come late */
#define PIP_DELAY_US 300000
#define PIP_PES_MAX (1024 * 1024)

static int64_t monotonic_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static cPip *instance = NULL;

cPip *cPip::getInstance()
{
	if (!instance)
		instance = new cPip();
	return instance;
}

cPip::cPip()
{
	mRunning = false;
	mQuit = false;
	mCancel[0] = mCancel[1] = -1;
	mUnit = 1;
	mVpid = 0;
	mVtype = 0;
	mShow = false;
	mDecoded = false;
	mWin.on = false;
	mWin.x = mWin.y = mWin.w = mWin.h = 0;
	mCtx = NULL;
	mParser = NULL;
	mHw = NULL;
	mPkt = NULL;
	mFrame = NULL;
	mPesOpen = false;
	mPesPts = AV_NOPTS_VALUE;
	mBadFrames = 0;
	mBase = INT64_MIN;
	mWake = NULL;
	mWakeCtx = NULL;
}

void cPip::setWake(void (*wake)(void *), void *ctx)
{
	OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
	mWake = wake;
	mWakeCtx = ctx;
}

void cPip::wake()
{
	void (*w)(void *);
	void *ctx;
	{
		OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
		w = mWake;
		ctx = mWakeCtx;
	}
	if (w)
		w(ctx);
}

bool cPip::pending()
{
	OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
	return !mFrames.empty();
}

bool cPip::start(int unit, int vtype)
{
	stop();
	uint16_t vpid, apid, pcrpid;
	hal_unit_pids(unit, vpid, apid, pcrpid);
	if (!vpid)
	{
		hal_info("PIP: unit %d has no video PID\n", unit);
		return false;
	}
	if (pipe2(mCancel, O_CLOEXEC | O_NONBLOCK) < 0)
		return false;
	mUnit = unit;
	mVpid = vpid;
	mVtype = vtype;
	mQuit = false;
	mRunning = true;
	if (OpenThreads::Thread::start() != 0)
	{
		mRunning = false;
		close(mCancel[0]);
		close(mCancel[1]);
		mCancel[0] = mCancel[1] = -1;
		return false;
	}
	hal_info("PIP: unit %d, video PID 0x%x, type %d\n", unit, vpid, vtype);
	return true;
}

void cPip::stop()
{
	if (!mRunning)
		return;
	mQuit = true;
	if (write(mCancel[1], "x", 1) < 0)
		; /* the poll times out, the flag is enough then */
	{
		OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
		mTaken.signal();
	}
	OpenThreads::Thread::join();
	close(mCancel[0]);
	close(mCancel[1]);
	mCancel[0] = mCancel[1] = -1;
	{
		OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
		while (!mFrames.empty())
		{
			av_frame_free(&mFrames.front());
			mFrames.pop_front();
		}
		mDecoded = false;
		mRunning = false;
	}
	wake();
}

void cPip::setRect(int x, int y, int w, int h, int osd_w, int osd_h)
{
	if (osd_w <= 0 || osd_h <= 0)
		return;
	{
		OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
		mWin.x = (double)x / osd_w;
		mWin.y = (double)y / osd_h;
		mWin.w = (double)w / osd_w;
		mWin.h = (double)h / osd_h;
	}
	wake();
}

void cPip::show(bool on)
{
	{
		OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
		mShow = on;
	}
	wake();
}

bool cPip::showing()
{
	OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
	return mRunning && mShow && mDecoded;
}

cPip::Window cPip::window()
{
	OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
	Window w = mWin;
	w.on = mRunning && mShow && w.w > 0 && w.h > 0;
	return w;
}

static int64_t frame_pts_us(AVFrame *f)
{
	int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
	return pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : pts * 100 / 9;
}

AVFrame *cPip::takeFrame()
{
	OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
	int64_t now = monotonic_us();
	if (mFrames.empty())
		return NULL;
	int64_t first = frame_pts_us(mFrames.front());
	int64_t last = frame_pts_us(mFrames.back());
	/* the clock starts with the first frame and starts again where the PTS
	 * jumps; where even the newest frame is late, the newest sets it */
	if (first != AV_NOPTS_VALUE && (mBase == INT64_MIN || llabs(first + mBase - now) > 2000000))
		mBase = now - first + PIP_DELAY_US;
	else if (last != AV_NOPTS_VALUE && last + mBase < now)
		mBase = now - last + PIP_DELAY_US;
	/* one frame a display frame, in order; only where more than two are due
	 * the oldest go, to catch up */
	size_t due = 0;
	for (size_t i = 0; i < mFrames.size(); i++)
	{
		int64_t pts = frame_pts_us(mFrames[i]);
		if (pts != AV_NOPTS_VALUE && pts + mBase > now)
			break;
		due++;
	}
	if (!due)
		return NULL;
	for (; due > 2; due--)
	{
		av_frame_free(&mFrames.front());
		mFrames.pop_front();
	}
	AVFrame *f = mFrames.front();
	mFrames.pop_front();
	mTaken.signal();
	return f;
}

static enum AVPixelFormat pip_get_format(AVCodecContext *, const enum AVPixelFormat *fmts)
{
	const enum AVPixelFormat *p;
	for (p = fmts; *p != AV_PIX_FMT_NONE; p++)
		if (*p == AV_PIX_FMT_DRM_PRIME)
			return *p;
	for (p = fmts; *p != AV_PIX_FMT_NONE; p++)
		if (!(av_pix_fmt_desc_get(*p)->flags & AV_PIX_FMT_FLAG_HWACCEL))
			return *p;
	return AV_PIX_FMT_NONE;
}

bool cPip::openDecoder()
{
	AVCodecID id = hal_codec_for_format((VIDEO_FORMAT)mVtype);
	const AVCodec *codec = avcodec_find_decoder(id);
	if (!codec)
	{
		hal_info("PIP: no decoder for stream type %d\n", mVtype);
		return false;
	}
	mCtx = avcodec_alloc_context3(codec);
	mParser = av_parser_init(id);
	mPkt = av_packet_alloc();
	mFrame = av_frame_alloc();
	if (!mCtx || !mParser || !mPkt || !mFrame)
		return false;
	mCtx->pkt_timebase = (AVRational) { 1, 90000 };
	/* the frames the GL thread holds on top of what the decoder needs */
	mCtx->extra_hw_frames = PIP_QUEUE + 2;
	/* by name: not every ffmpeg knows the stateless V4L2 decoders */
	static const char *const hw[] = { "v4l2request", "drm" };
	for (unsigned i = 0; i < sizeof(hw) / sizeof(hw[0]) && !mHw; i++)
	{
		enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hw[i]);
		if (type == AV_HWDEVICE_TYPE_NONE || av_hwdevice_ctx_create(&mHw, type, NULL, NULL, 0) < 0)
			mHw = NULL;
	}
	if (mHw)
	{
		mCtx->hw_device_ctx = av_buffer_ref(mHw);
		mCtx->get_format = pip_get_format;
	}
	if (avcodec_open2(mCtx, codec, NULL) < 0)
	{
		hal_info("PIP: cannot open %s\n", codec->name);
		return false;
	}
	hal_info("PIP: %s decoder, %s\n", codec->name, mHw ? av_hwdevice_get_type_name(((AVHWDeviceContext *)mHw->data)->type) : "software");
	return true;
}

void cPip::closeDecoder()
{
	if (mParser)
		av_parser_close(mParser);
	mParser = NULL;
	av_packet_free(&mPkt);
	av_frame_free(&mFrame);
	avcodec_free_context(&mCtx);
	av_buffer_unref(&mHw);
	mPes.clear();
}

void cPip::push(AVFrame *f)
{
	/* only frames that stay in the decoder's memory go on a plane */
	if (f->format != AV_PIX_FMT_DRM_PRIME)
	{
		if (!mBadFrames++)
			hal_info("PIP: decoded to %s, which cannot be shown\n", av_get_pix_fmt_name((AVPixelFormat)f->format));
		av_frame_free(&f);
		return;
	}
	{
		OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mLock);
		while (mFrames.size() >= PIP_QUEUE && !mQuit)
			mTaken.wait(&mLock, 200);
		mFrames.push_back(f);
		mDecoded = true;
	}
	wake();
}

void cPip::decode(const uint8_t *data, int size, int64_t pts)
{
	while (size > 0 && mFrame)
	{
		uint8_t *out = NULL;
		int out_size = 0;
		int n = av_parser_parse2(mParser, mCtx, &out, &out_size, data, size, pts, pts, 0);
		if (n < 0)
			break;
		data += n;
		size -= n;
		pts = AV_NOPTS_VALUE;
		if (!out_size)
			continue;
		mPkt->data = out;
		mPkt->size = out_size;
		mPkt->pts = mParser->pts;
		mPkt->dts = mParser->dts;
		if (avcodec_send_packet(mCtx, mPkt) < 0)
			continue;
		while (avcodec_receive_frame(mCtx, mFrame) == 0)
		{
			push(mFrame);
			mFrame = av_frame_alloc();
			if (!mFrame)
				break;
		}
	}
}

/* a PES packet goes to the parser whole, when the next one starts */
void cPip::flush()
{
	if (!mPes.empty())
		decode(mPes.data(), mPes.size(), mPesPts);
	mPes.clear();
	mPesPts = AV_NOPTS_VALUE;
}

/* a transport packet of the video PID: the PES header gives the PTS, the
 * payload is collected */
void cPip::packet(const uint8_t *ts)
{
	int pid = ((ts[1] & 0x1f) << 8) | ts[2];
	if (pid != mVpid || (ts[1] & 0x80))
		return;
	int afc = (ts[3] >> 4) & 3;
	if (!(afc & 1))
		return;
	int off = 4;
	if (afc & 2)
		off += 1 + ts[4];
	if (off >= TS_SIZE)
		return;
	const uint8_t *p = ts + off;
	int len = TS_SIZE - off;
	if (ts[1] & 0x40)
	{
		flush();
		int hdr = len >= 9 ? 9 + p[8] : len + 1;
		if (len < 9 || p[0] || p[1] || p[2] != 1 || hdr > len)
		{
			mPesOpen = false;
			return;
		}
		if ((p[7] & 0x80) && len >= 14)
			mPesPts = ((int64_t)(p[9] & 0x0e) << 29) | (p[10] << 22) | ((p[11] & 0xfe) << 14) |
				  (p[12] << 7) | (p[13] >> 1);
		mPesOpen = true;
		mPes.insert(mPes.end(), p + hdr, p + len);
		return;
	}
	if (!mPesOpen)
		return;
	mPes.insert(mPes.end(), p, p + len);
	/* a video PES may have no length: one that grows this far goes on */
	if (mPes.size() >= PIP_PES_MAX)
	{
		decode(mPes.data(), mPes.size(), mPesPts);
		mPes.clear();
		mPesPts = AV_NOPTS_VALUE;
	}
}

void cPip::run()
{
	hal_set_threadname("hal:pip");
	cDemux dmx(mUnit);
	if (!dmx.Open(DMX_TP_CHANNEL, NULL, PIP_DMX_BUFFER) || !dmx.pesFilter(mVpid) || !dmx.Start())
	{
		hal_info("PIP: demux unit %d does not open\n", mUnit);
		return;
	}
	if (!openDecoder())
	{
		closeDecoder();
		return;
	}
	mPesOpen = false;
	mPes.clear();
	mPes.reserve(256 * 1024);
	mPesPts = AV_NOPTS_VALUE;
	mBadFrames = 0;
	mBase = INT64_MIN;

	uint8_t buf[TS_SIZE * 64];
	int have = 0;
	struct pollfd pfd[2];
	pfd[0].fd = dmx.getFD();
	pfd[0].events = POLLIN;
	pfd[1].fd = mCancel[0];
	pfd[1].events = POLLIN;
	while (!mQuit)
	{
		pfd[0].revents = pfd[1].revents = 0;
		if (poll(pfd, 2, 1000) <= 0 || mQuit || (pfd[1].revents & POLLIN))
			continue;
		ssize_t n = read(pfd[0].fd, buf + have, sizeof(buf) - have);
		if (n <= 0)
		{
			/* EOVERFLOW: the demux buffer ran over and was flushed */
			if (n < 0 && errno != EAGAIN && errno != EINTR && errno != EOVERFLOW)
				break;
			continue;
		}
		have += n;
		int pos = 0;
		while (have - pos >= TS_SIZE)
		{
			if (buf[pos] != 0x47)
			{
				pos++;
				mPesOpen = false;
				mPes.clear();
				continue;
			}
			packet(buf + pos);
			pos += TS_SIZE;
		}
		memmove(buf, buf + pos, have - pos);
		have -= pos;
	}
	dmx.Stop();
	closeDecoder();
}
