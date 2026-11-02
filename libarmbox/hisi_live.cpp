/*
 * Live TV of the HiSilicon boxes through the vendor AVPLAY, see hisi_live.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <config.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <thread>
#include <time.h>
#include <unistd.h>

#include <hardware_caps.h>
#include <dmx_hal.h>

#include "hisi_live.h"
#include "hisi_unf.h"
#include "hal_debug.h"

#define hal_debug(args...) _hal_debug(HAL_DEBUG_VIDEO, NULL, args)
#define hal_info(args...) _hal_info(HAL_DEBUG_VIDEO, NULL, args)

#define HISI_LIB_DIR "/usr/lib/hisilicon/"
/* a demux stays attached when Neutrino dies; the next one takes it again */
#define HISI_DEMUX_FILE "/tmp/hisi-live-demux"

/* zapit's audio types */
enum { AUDIO_AC3 = 0, AUDIO_MPEG = 1, AUDIO_DTS = 2, AUDIO_LPCM = 6, AUDIO_AAC = 8, AUDIO_AACPLUS = 9,
	AUDIO_DTSHD = 0x10, AUDIO_EAC3 = 0x22 };
/* libstb-hal's VIDEO_FORMAT */
enum { FORMAT_MPEG2 = 0, FORMAT_H264 = 1, FORMAT_VC1 = 2, FORMAT_H265 = 6, FORMAT_AVS = 16 };

typedef hi_s32 (*event_cb)(hi_handle, hi_u32, hi_u32);

/* what the libraries fill in: those of a box may be newer than the SDK
 * layout of hisi_unf.h and write more, as GetStreamInfo of the HD60 does */
template <typename T> struct filled
{
	T v;
	char spare[512];
};

/* the functions of libhi_msp and libhi_common that are used */
#define HISI_FUNCTIONS \
	F(HI_SYS_Init, (void)) \
	F(HI_UNF_DMX_Init, (void)) \
	F(HI_UNF_DMX_GetCapability, (hi_dmx_capability *)) \
	F(HI_UNF_DMX_GetTSPortId, (hi_u32, hi_u32 *)) \
	F(HI_UNF_DMX_AttachTSPort, (hi_u32, hi_u32)) \
	F(HI_UNF_DMX_DetachTSPort, (hi_u32)) \
	F(HI_UNF_DISP_Init, (void)) \
	F(HI_UNF_VO_Init, (hi_u32)) \
	F(HI_UNF_VO_CreateWindow, (const hi_window_attr *, hi_handle *)) \
	F(HI_UNF_VO_DestroyWindow, (hi_handle)) \
	F(HI_UNF_VO_AttachWindow, (hi_handle, hi_handle)) \
	F(HI_UNF_VO_DetachWindow, (hi_handle, hi_handle)) \
	F(HI_UNF_VO_SetWindowEnable, (hi_handle, hi_bool)) \
	F(HI_UNF_VO_SetWindowZorder, (hi_handle, hi_u32)) \
	F(HI_UNF_VO_ResetWindow, (hi_handle, hi_u32)) \
	F(HI_UNF_VO_FreezeWindow, (hi_handle, hi_bool, hi_u32)) \
	F(HI_UNF_VO_GetWindowAttr, (hi_handle, hi_window_attr *)) \
	F(HI_UNF_VO_SetWindowAttr, (hi_handle, const hi_window_attr *)) \
	F(HI_UNF_DISP_GetVirtualScreen, (hi_u32, hi_u32 *, hi_u32 *)) \
	F(HI_UNF_DISP_SetAspectRatio, (hi_u32, hi_disp_aspect_ratio *)) \
	F(HI_UNF_VO_CapturePicture, (hi_handle, hi_video_frame_info *)) \
	F(HI_UNF_VO_CapturePictureRelease, (hi_handle, hi_video_frame_info *)) \
	F(HI_MMZ_Unmap, (hi_u32)) \
	F(HI_UNF_SND_Init, (void)) \
	F(HI_UNF_SND_GetDefaultOpenAttr, (hi_u32, hi_snd_attr *)) \
	F(HI_UNF_SND_Open, (hi_u32, const hi_snd_attr *)) \
	F(HI_UNF_SND_GetDefaultTrackAttr, (hi_u32, hi_audiotrack_attr *)) \
	F(HI_UNF_SND_CreateTrack, (hi_u32, const hi_audiotrack_attr *, hi_handle *)) \
	F(HI_UNF_SND_DestroyTrack, (hi_handle)) \
	F(HI_UNF_SND_Attach, (hi_handle, hi_handle)) \
	F(HI_UNF_SND_Detach, (hi_handle, hi_handle)) \
	F(HI_UNF_SND_SetTrackMute, (hi_handle, hi_bool)) \
	F(HI_UNF_AVPLAY_Init, (void)) \
	F(HI_UNF_AVPLAY_RegisterAcodecLib, (const char *)) \
	F(HI_UNF_AVPLAY_GetDefaultConfig, (hi_avplay_attr *, hi_u32)) \
	F(HI_UNF_AVPLAY_Create, (const hi_avplay_attr *, hi_handle *)) \
	F(HI_UNF_AVPLAY_Destroy, (hi_handle)) \
	F(HI_UNF_AVPLAY_ChnOpen, (hi_handle, hi_u32, const void *)) \
	F(HI_UNF_AVPLAY_ChnClose, (hi_handle, hi_u32)) \
	F(HI_UNF_AVPLAY_GetAttr, (hi_handle, hi_u32, void *)) \
	F(HI_UNF_AVPLAY_SetAttr, (hi_handle, hi_u32, void *)) \
	F(HI_UNF_AVPLAY_Start, (hi_handle, hi_u32, const void *)) \
	F(HI_UNF_AVPLAY_Stop, (hi_handle, hi_u32, const hi_avplay_stop_opt *)) \
	F(HI_UNF_AVPLAY_GetStatusInfo, (hi_handle, hi_avplay_status_info *)) \
	F(HI_UNF_AVPLAY_GetStreamInfo, (hi_handle, hi_avplay_stream_info *)) \
	F(HI_UNF_AVPLAY_RegisterEvent, (hi_handle, hi_u32, event_cb))

#define AVPLAY_EVENT_NEW_VID_FRAME 5
#define AVPLAY_EVENT_FIRST_FRAME_DISPLAYED 20

struct cHisiLive::priv
{
#define F(name, args) hi_s32 (*name) args;
	HISI_FUNCTIONS
#undef F
	std::mutex mutex;
	hi_handle avplay = 0, window = 0, track = 0;
	hi_u32 dmx = 0;
	unsigned short pid[PID_COUNT] = { 0, 0, 0 };
	int video_format = FORMAT_MPEG2;
	int audio_type = AUDIO_MPEG;
	bool video_running = false, audio_running = false, shown = false;
	/* a start shows the window frozen, black or with the last picture,
	 * until audio and video are in sync; each start counts up */
	bool hold_last = false;
	unsigned start_count = 0;
	/* AC3, E-AC3 and DTS through libHA.AUDIO.MULTI and the vendor's FFmpeg
	 * 3, as the vendor service decodes them */
	hi_s32 (*multi_config)(void *, hi_s32 *) = nullptr;
	void *(*mmz_map)(hi_u32, hi_u32) = nullptr;
};

/* a library of the vendor: it finds its own ones through its run path
 * (hisi-libs); lazily, as libavformat.so.57 refers to functions of the
 * vendor service that the audio decoding never calls */
static void *load_vendor(const char *name)
{
	char path[128];
	snprintf(path, sizeof(path), HISI_LIB_DIR "%s", name);
	void *h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
	if (!h)
		hal_info("hisi live: %s\n", dlerror());
	return h;
}

static long long now_ms()
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

/* with HAL_HISI_DEBUG: the time to the first frame and the frames in each
 * quarter second after a start, which shows a slow start */
static std::atomic<long long> video_start(0);
static std::atomic<bool> first_frame(false);
static std::atomic<unsigned> frame_bins[12];
static bool debug_frames;

static hi_s32 on_event(hi_handle, hi_u32 event, hi_u32)
{
	long long start = video_start;
	if (!start)
		return HI_SUCCESS;
	long long t = now_ms() - start;
	if (event == AVPLAY_EVENT_FIRST_FRAME_DISPLAYED)
	{
		first_frame = true;
		hal_info("hisi live: first frame after %lld ms\n", t);
	}
	else if (event == AVPLAY_EVENT_NEW_VID_FRAME && debug_frames)
	{
		if (t < 3000)
			frame_bins[t / 250]++;
		else if (video_start.exchange(0))
		{
			char line[80];
			int n = 0;
			for (unsigned i = 0; i < 12; i++)
				n += snprintf(line + n, sizeof(line) - n, " %u", frame_bins[i].exchange(0));
			hal_info("hisi live: frames per 250 ms:%s\n", line);
		}
	}
	return HI_SUCCESS;
}

cHisiLive *cHisiLive::getInstance()
{
	static cHisiLive *instance = nullptr;
	static std::once_flag once;
	std::call_once(once, []
	{
		const char *env = getenv("HAL_HISI");
		if (!get_hwcaps()->can_hisi_avplay || (env && !strcmp(env, "0")))
			return;
		cHisiLive *live = new cHisiLive();
		if (live->Init())
			instance = live;
		else
			delete live;
	});
	return instance;
}

bool cHisiLive::Init()
{
	/* libhi_msp resolves libhi_common's symbols globally, and the
	 * codec libraries it loads later resolve its own */
	if (!dlopen(HISI_LIB_DIR "libhi_common.so", RTLD_NOW | RTLD_GLOBAL))
	{
		hal_info("hisi live: %s\n", dlerror());
		return false;
	}
	void *msp = dlopen(HISI_LIB_DIR "libhi_msp.so", RTLD_NOW | RTLD_GLOBAL);
	if (!msp)
	{
		hal_info("hisi live: %s\n", dlerror());
		return false;
	}
	p = new priv;
#define F(name, args) \
	if (!(*(void **)&p->name = dlsym(RTLD_DEFAULT, #name))) \
	{ \
		hal_info("hisi live: %s missing\n", #name); \
		return false; \
	}
	HISI_FUNCTIONS
#undef F
	*(void **)&p->mmz_map = dlsym(RTLD_DEFAULT, "HI_MMZ_Map");

#define CHECK(call) \
	if (hi_s32 r_ = (call)) \
	{ \
		hal_info("hisi live: %s failed: %#x\n", #call, r_); \
		return false; \
	}
	CHECK(p->HI_SYS_Init());
	CHECK(p->HI_UNF_DMX_Init());

	/* the emulated devices keep their demuxes attached; ours is a free one */
	filled<hi_dmx_capability> caps;
	hi_dmx_capability &cap = caps.v;
	CHECK(p->HI_UNF_DMX_GetCapability(&cap));
	bool found = false;
	if (FILE *f = fopen(HISI_DEMUX_FILE, "r"))
	{
		found = fscanf(f, "%u", &p->dmx) == 1 && p->dmx < cap.dmx_num;
		fclose(f);
	}
	for (hi_u32 i = cap.dmx_num; i-- > 0 && !found;)
	{
		hi_u32 port;
		if (p->HI_UNF_DMX_GetTSPortId(i, &port) != HI_SUCCESS)
		{
			p->dmx = i;
			found = true;
		}
	}
	if (!found)
	{
		hal_info("hisi live: no free demux of %u\n", cap.dmx_num);
		return false;
	}
	if (FILE *f = fopen(HISI_DEMUX_FILE, "w"))
	{
		fprintf(f, "%u\n", p->dmx);
		fclose(f);
	}

	CHECK(p->HI_UNF_DISP_Init());
	CHECK(p->HI_UNF_VO_Init(HI_UNF_VO_DEV_MODE_NORMAL));
	hi_window_attr win;
	memset(&win, 0, sizeof(win));
	win.disp = HI_UNF_DISPLAY1;
	win.aspect_cvrs = HI_UNF_VO_ASPECT_CVRS_IGNORE;
	CHECK(p->HI_UNF_VO_CreateWindow(&win, &p->window));

	CHECK(p->HI_UNF_SND_Init());
	filled<hi_snd_attr> snd;
	p->HI_UNF_SND_GetDefaultOpenAttr(HI_UNF_SND_0, &snd.v);
	/* opened by the vendor service already, which is fine */
	p->HI_UNF_SND_Open(HI_UNF_SND_0, &snd.v);

	CHECK(p->HI_UNF_AVPLAY_Init());
	static const char *codecs[] = { "MP2", "MP3", "AAC" };
	for (const char *c : codecs)
	{
		char lib[80];
		snprintf(lib, sizeof(lib), HISI_LIB_DIR "libHA.AUDIO.%s.decode.so", c);
		if (p->HI_UNF_AVPLAY_RegisterAcodecLib(lib) != HI_SUCCESS)
			hal_info("hisi live: no %s decoder\n", c);
	}
	/* the MULTI decoder keeps a codec per decoder slot and finds the slot
	 * of a decoding thread in a table it is given; an empty one puts every
	 * thread into slot 0. Its configuration takes no decoder handle. */
	static hi_s32 slots[20];
	if (void *multi = load_vendor("libHA.AUDIO.MULTI.decode.so"))
	{
		void (*set_codecuserinfo)(void *);
		*(void **)&set_codecuserinfo = dlsym(multi, "set_codecuserinfo");
		void **entry = (void **)dlsym(multi, "ha_audio_decode_entry");
		if (set_codecuserinfo && entry
		    && p->HI_UNF_AVPLAY_RegisterAcodecLib(HISI_LIB_DIR "libHA.AUDIO.MULTI.decode.so") == HI_SUCCESS)
		{
			set_codecuserinfo(slots);
			/* HI_HA_DECODE_S: name, id, version, description, 2 reserved, init, deinit, config */
			*(void **)&p->multi_config = entry[8];
		}
	}
	if (!p->multi_config)
		hal_info("hisi live: AC3, E-AC3 and DTS stay on the emulated decoder\n");
	filled<hi_avplay_attr> attr;
	CHECK(p->HI_UNF_AVPLAY_GetDefaultConfig(&attr.v, HI_UNF_AVPLAY_STREAM_TYPE_TS));
	attr.v.demux_id = p->dmx;
	CHECK(p->HI_UNF_AVPLAY_Create(&attr.v, &p->avplay));
	CHECK(p->HI_UNF_AVPLAY_ChnOpen(p->avplay, HI_UNF_AVPLAY_MEDIA_CHAN_VID, NULL));
	CHECK(p->HI_UNF_AVPLAY_ChnOpen(p->avplay, HI_UNF_AVPLAY_MEDIA_CHAN_AUD, NULL));
	CHECK(p->HI_UNF_VO_AttachWindow(p->window, p->avplay));

	filled<hi_audiotrack_attr> track;
	CHECK(p->HI_UNF_SND_GetDefaultTrackAttr(HI_UNF_SND_TRACK_TYPE_MASTER, &track.v));
	CHECK(p->HI_UNF_SND_CreateTrack(HI_UNF_SND_0, &track.v, &p->track));
	CHECK(p->HI_UNF_SND_Attach(p->track, p->avplay));

	debug_frames = getenv("HAL_HISI_DEBUG") != NULL;
	p->HI_UNF_AVPLAY_RegisterEvent(p->avplay, AVPLAY_EVENT_FIRST_FRAME_DISPLAYED, on_event);
	if (debug_frames)
		p->HI_UNF_AVPLAY_RegisterEvent(p->avplay, AVPLAY_EVENT_NEW_VID_FRAME, on_event);
#undef CHECK
	hal_info("hisi live: AVPLAY on demux %u\n", p->dmx);
	return true;
}

/* the demux to the port of the tuner the live channel comes from; the
 * players of the emulated devices may have taken it meanwhile */
bool cHisiLive::AttachDemux()
{
	hi_u32 port = HI_UNF_DMX_PORT_TSI_0 + cDemux::GetSource(0);
	hi_u32 now;
	if (p->HI_UNF_DMX_GetTSPortId(p->dmx, &now) == HI_SUCCESS)
	{
		if (now == port)
			return true;
		p->HI_UNF_DMX_DetachTSPort(p->dmx);
	}
	if (p->HI_UNF_DMX_AttachTSPort(p->dmx, port) == HI_SUCCESS)
		return true;
	hal_info("hisi live: demux %u not attached to port %#x\n", p->dmx, port);
	return false;
}

/* to the audio, else the PCR. Only with slow playing does the AVPLAY
 * bring them together and keep them so, which StartVideo hides behind a
 * frozen window until they are */
void cHisiLive::SetSync(unsigned ref)
{
	filled<hi_sync_attr> attr;
	hi_sync_attr &sync = attr.v;
	if (p->HI_UNF_AVPLAY_GetAttr(p->avplay, HI_UNF_AVPLAY_ATTR_ID_SYNC, &sync) != HI_SUCCESS)
		return;
	sync.sync_ref = ref;
	sync.start_region.smooth_play = 1;
	sync.novel_region.smooth_play = 1;
	sync.quick_output = 0;
	if (p->HI_UNF_AVPLAY_SetAttr(p->avplay, HI_UNF_AVPLAY_ATTR_ID_SYNC, &sync) != HI_SUCCESS)
		hal_info("hisi live: sync %u not set\n", ref);
}

void cHisiLive::SetPid(int which, unsigned short pid)
{
	std::lock_guard<std::mutex> lock(p->mutex);
	if (which >= 0 && which < PID_COUNT)
		p->pid[which] = pid;
}

void cHisiLive::SetVideoType(int video_format)
{
	std::lock_guard<std::mutex> lock(p->mutex);
	p->video_format = video_format;
}

void cHisiLive::SetAudioType(int zapit_type)
{
	std::lock_guard<std::mutex> lock(p->mutex);
	p->audio_type = zapit_type;
}

bool cHisiLive::StartVideo()
{
	std::lock_guard<std::mutex> lock(p->mutex);
	if (p->video_running || !p->pid[PID_VIDEO] || !AttachDemux())
		return false;
	hi_u32 type;
	switch (p->video_format)
	{
		case FORMAT_H264: type = HI_UNF_VCODEC_TYPE_H264; break;
		case FORMAT_H265: type = HI_UNF_VCODEC_TYPE_HEVC; break;
		case FORMAT_VC1: type = HI_UNF_VCODEC_TYPE_VC1; break;
		case FORMAT_AVS: type = HI_UNF_VCODEC_TYPE_AVS; break;
		default: type = HI_UNF_VCODEC_TYPE_MPEG2; break;
	}
	filled<hi_vcodec_attr> attr;
	hi_vcodec_attr &vdec = attr.v;
	if (p->HI_UNF_AVPLAY_GetAttr(p->avplay, HI_UNF_AVPLAY_ATTR_ID_VDEC, &vdec) == HI_SUCCESS)
	{
		vdec.type = type;
		vdec.mode = HI_UNF_VCODEC_MODE_NORMAL;
		vdec.err_cover = 100;
		vdec.priority = 3;
		p->HI_UNF_AVPLAY_SetAttr(p->avplay, HI_UNF_AVPLAY_ATTR_ID_VDEC, &vdec);
	}
	if (!p->audio_running)
		SetSync(p->pid[PID_PCR] && p->pid[PID_PCR] != 0x1fff ? HI_UNF_SYNC_REF_PCR : HI_UNF_SYNC_REF_NONE);
	hi_u32 vpid = p->pid[PID_VIDEO], pcr = p->pid[PID_PCR] ? p->pid[PID_PCR] : 0x1fff;
	p->HI_UNF_AVPLAY_SetAttr(p->avplay, HI_UNF_AVPLAY_ATTR_ID_PCR_PID, &pcr);
	p->HI_UNF_AVPLAY_SetAttr(p->avplay, HI_UNF_AVPLAY_ATTR_ID_VID_PID, &vpid);
	if (!p->shown)
	{
		p->HI_UNF_VO_SetWindowEnable(p->window, 1);
		p->HI_UNF_VO_SetWindowZorder(p->window, HI_LAYER_ZORDER_MOVETOP);
		p->shown = true;
	}
	/* black, or the picture held by the zapping mode or a pause, and
	 * silent until the slow start is over */
	p->HI_UNF_VO_FreezeWindow(p->window, 1, p->hold_last ? HI_UNF_WINDOW_FREEZE_MODE_LAST : HI_UNF_WINDOW_FREEZE_MODE_BLACK);
	p->HI_UNF_SND_SetTrackMute(p->track, 1);
	first_frame = false;
	video_start = now_ms();
	hi_s32 r = p->HI_UNF_AVPLAY_Start(p->avplay, HI_UNF_AVPLAY_MEDIA_CHAN_VID, NULL);
	if (r != HI_SUCCESS)
	{
		p->HI_UNF_VO_FreezeWindow(p->window, 0, HI_UNF_WINDOW_FREEZE_MODE_LAST);
		p->HI_UNF_SND_SetTrackMute(p->track, 0);
		hal_info("hisi live: video %#x (type %u) not started: %#x\n", vpid, type, r);
		return false;
	}
	p->video_running = true;
	std::thread(&cHisiLive::Reveal, this, ++p->start_count).detach();
	return true;
}

/* window and sound come once the picture runs with the audio, or with its
 * first frame without audio; at the latest after four seconds */
void cHisiLive::Reveal(unsigned start)
{
	long long t0 = now_ms();
	int last = INT32_MAX, steady = 0;
	for (;;)
	{
		usleep(40000);
		std::lock_guard<std::mutex> lock(p->mutex);
		if (start != p->start_count || !p->video_running)
			return;
		bool in_sync = false;
		if (first_frame)
		{
			/* slow playing closes the gap by some 20 ms each round; it is
			 * over when the gap stops shrinking */
			filled<hi_avplay_status_info> st;
			if (!p->audio_running)
				in_sync = true;
			else if (p->HI_UNF_AVPLAY_GetStatusInfo(p->avplay, &st.v) == HI_SUCCESS)
			{
				int diff = st.v.diff_av_play_time;
				steady = abs(diff) <= 100 && abs(diff - last) < 8 ? steady + 1 : 0;
				last = diff;
				in_sync = steady >= 3;
			}
		}
		long long t = now_ms() - t0;
		if (in_sync || t > 4000)
		{
			p->HI_UNF_VO_FreezeWindow(p->window, 0, HI_UNF_WINDOW_FREEZE_MODE_LAST);
			p->HI_UNF_SND_SetTrackMute(p->track, 0);
			if (debug_frames)
				hal_info("hisi live: shown after %lld ms%s\n", t, in_sync ? "" : ", not in sync");
			return;
		}
	}
}

void cHisiLive::StopVideo(bool blank)
{
	std::lock_guard<std::mutex> lock(p->mutex);
	if (p->video_running)
	{
		hi_avplay_stop_opt stop = { (hi_u32)(blank ? HI_UNF_AVPLAY_STOP_MODE_BLACK : HI_UNF_AVPLAY_STOP_MODE_STILL), 0 };
		p->HI_UNF_AVPLAY_Stop(p->avplay, HI_UNF_AVPLAY_MEDIA_CHAN_VID, &stop);
		p->video_running = false;
	}
	p->hold_last = !blank;
	/* radio, which may follow, has no picture to wait for */
	p->HI_UNF_SND_SetTrackMute(p->track, 0);
	/* the stop mode alone leaves the last frame in a window that stays on */
	if (blank && p->shown)
		p->HI_UNF_VO_ResetWindow(p->window, HI_UNF_WINDOW_FREEZE_MODE_BLACK);
}

/* the codec of the MULTI decoder for an audio type, 0 for the HA decoders */
static hi_s32 multi_codec(int zapit_type)
{
	switch (zapit_type)
	{
		case AUDIO_AC3: return 3;
		case AUDIO_DTS:
		case AUDIO_DTSHD: return 4;
		case AUDIO_EAC3: return 43;
		default: return 0;
	}
}

bool cHisiLive::DecodesAudio()
{
	std::lock_guard<std::mutex> lock(p->mutex);
	int t = p->audio_type;
	return t == AUDIO_MPEG || t == AUDIO_AAC || t == AUDIO_AACPLUS || (p->multi_config && multi_codec(t));
}

bool cHisiLive::StartAudio()
{
	std::lock_guard<std::mutex> lock(p->mutex);
	if (p->audio_running || !p->pid[PID_AUDIO] || !AttachDemux())
		return false;
	hi_u32 type;
	hi_s32 multi = p->multi_config ? multi_codec(p->audio_type) : 0;
	switch (p->audio_type)
	{
		case AUDIO_MPEG: type = HA_AUDIO_ID_MP2; break;
		case AUDIO_AAC:
		case AUDIO_AACPLUS: type = HA_AUDIO_ID_AAC; break;
		default:
			if (!multi)
				return false;
			hi_s32 config[2] = { (hi_s32)0xffff1000, multi };
			if (p->multi_config(NULL, config) != HI_SUCCESS)
			{
				hal_info("hisi live: MULTI decoder codec %d not set\n", multi);
				return false;
			}
			type = HA_AUDIO_ID_CUSTOM_1;
			break;
	}
	filled<hi_acodec_attr> attr;
	hi_acodec_attr &adec = attr.v;
	if (p->HI_UNF_AVPLAY_GetAttr(p->avplay, HI_UNF_AVPLAY_ATTR_ID_ADEC, &adec) != HI_SUCCESS)
		return false;
	adec.type = type;
	hi_hadecode_openparam &o = adec.decode_param;
	o.dec_mode = HD_DEC_MODE_RAWPCM;
	o.codec_private_data = NULL;
	o.codec_private_data_size = 0;
	/* the SDK defaults: MPEG audio interleaved stereo, AAC up to 8 planar
	 * channels; MULTI downmixes to interleaved stereo as well */
	o.pcm_format.desired_out_channels = type == HA_AUDIO_ID_AAC ? 8 : 2;
	o.pcm_format.interleaved = type != HA_AUDIO_ID_AAC;
	o.pcm_format.bit_per_sample = 16;
	o.pcm_format.desired_sample_rate = 48000;
	memset(o.pcm_format.channel_mapping, 0, sizeof(o.pcm_format.channel_mapping));
	if (p->HI_UNF_AVPLAY_SetAttr(p->avplay, HI_UNF_AVPLAY_ATTR_ID_ADEC, &adec) != HI_SUCCESS)
	{
		hal_info("hisi live: audio decoder %#x not set\n", type);
		return false;
	}
	if (!p->video_running)
		SetSync(HI_UNF_SYNC_REF_AUDIO);
	hi_u32 apid = p->pid[PID_AUDIO];
	p->HI_UNF_AVPLAY_SetAttr(p->avplay, HI_UNF_AVPLAY_ATTR_ID_AUD_PID, &apid);
	hi_s32 r = p->HI_UNF_AVPLAY_Start(p->avplay, HI_UNF_AVPLAY_MEDIA_CHAN_AUD, NULL);
	if (r != HI_SUCCESS)
	{
		hal_info("hisi live: audio %#x not started: %#x\n", apid, r);
		return false;
	}
	p->audio_running = true;
	return true;
}

void cHisiLive::StopAudio()
{
	std::lock_guard<std::mutex> lock(p->mutex);
	if (!p->audio_running)
		return;
	p->HI_UNF_AVPLAY_Stop(p->avplay, HI_UNF_AVPLAY_MEDIA_CHAN_AUD, NULL);
	p->audio_running = false;
}

void cHisiLive::Hide()
{
	StopVideo(true);
	/* off rather than black, which would cover the emulated devices; a
	 * window switched off and on shows its last frame again, so a zap
	 * keeps it on */
	std::lock_guard<std::mutex> lock(p->mutex);
	if (p->shown)
	{
		p->HI_UNF_VO_FreezeWindow(p->window, 0, HI_UNF_WINDOW_FREEZE_MODE_LAST);
		p->HI_UNF_VO_SetWindowEnable(p->window, 0);
		p->shown = false;
	}
}

int64_t cHisiLive::GetPts()
{
	std::lock_guard<std::mutex> lock(p->mutex);
	filled<hi_avplay_status_info> info;
	hi_avplay_status_info &status = info.v;
	if (!p->video_running || p->HI_UNF_AVPLAY_GetStatusInfo(p->avplay, &status) != HI_SUCCESS)
		return 0;
	/* in ms, DVB counts 90 kHz */
	return (int64_t)status.last_vid_pts * 90;
}

bool cHisiLive::PictureInfo(int &width, int &height, int &rate_mhz)
{
	std::lock_guard<std::mutex> lock(p->mutex);
	filled<hi_avplay_stream_info> stream;
	hi_avplay_stream_info &info = stream.v;
	if (!p->video_running || p->HI_UNF_AVPLAY_GetStreamInfo(p->avplay, &info) != HI_SUCCESS)
		return false;
	width = info.width;
	height = info.height;
	rate_mhz = info.fps_integer * 1000 + info.fps_decimal;
	filled<hi_avplay_status_info> st;
	if (debug_frames && p->HI_UNF_AVPLAY_GetStatusInfo(p->avplay, &st.v) == HI_SUCCESS)
		hal_info("hisi live: audio - video %d ms (pts audio %u video %u, play %u local %u)\n", st.v.diff_av_play_time,
			 st.v.last_aud_pts, st.v.last_vid_pts, st.v.play_time, st.v.local_time);
	hal_debug("hisi live: %ux%u at %u.%03u fps\n", info.width, info.height, info.fps_integer, info.fps_decimal);
	return true;
}

/* Neutrino's screens: 4:3, 14:9, 16:9; the window converts to the
 * display's, which the emulated device's /proc/stb/video/aspect leaves */
void cHisiLive::SetScreenAspect(int aspect)
{
	hi_disp_aspect_ratio r = { 0, 0, 0 };
	switch (aspect)
	{
		case 1: r.aspect_ratio = HI_UNF_DISP_ASPECT_RATIO_4TO3; break;
		case 2: r = { HI_UNF_DISP_ASPECT_RATIO_USER, 14, 9 }; break;
		case 3: r.aspect_ratio = HI_UNF_DISP_ASPECT_RATIO_16TO9; break;
		default: return;
	}
	std::lock_guard<std::mutex> lock(p->mutex);
	if (p->HI_UNF_DISP_SetAspectRatio(HI_UNF_DISPLAY1, &r) != HI_SUCCESS)
		hal_info("hisi live: screen aspect %d not set\n", aspect);
}

/* Neutrino's modes: letterbox, panscan, bestfit, nonlinear */
void cHisiLive::SetAspectMode(int mode)
{
	static const hi_u32 cvrs[] = { HI_UNF_VO_ASPECT_CVRS_LETTERBOX, HI_UNF_VO_ASPECT_CVRS_PAN_SCAN,
		HI_UNF_VO_ASPECT_CVRS_COMBINED, HI_UNF_VO_ASPECT_CVRS_IGNORE };
	if (mode < 0 || mode > 3)
		return;
	std::lock_guard<std::mutex> lock(p->mutex);
	filled<hi_window_attr> attr;
	if (p->HI_UNF_VO_GetWindowAttr(p->window, &attr.v) != HI_SUCCESS)
		return;
	attr.v.aspect_cvrs = cvrs[mode];
	if (p->HI_UNF_VO_SetWindowAttr(p->window, &attr.v) != HI_SUCCESS)
		hal_info("hisi live: aspect conversion %u not set\n", cvrs[mode]);
}

/* in the PAL plane of the emulated device's /proc/stb/vmpeg, the whole
 * screen with a width of 0 */
void cHisiLive::SetWindowRect(int x, int y, int w, int h)
{
	std::lock_guard<std::mutex> lock(p->mutex);
	filled<hi_window_attr> attr;
	if (p->HI_UNF_VO_GetWindowAttr(p->window, &attr.v) != HI_SUCCESS)
		return;
	hi_u32 vw = 1280, vh = 720;
	p->HI_UNF_DISP_GetVirtualScreen(HI_UNF_DISPLAY1, &vw, &vh);
	hi_rect &r = attr.v.output_rect;
	if (w <= 0 || h <= 0)
		r = { 0, 0, 0, 0 };
	else
		r = { (hi_s32)(x * vw / 720), (hi_s32)(y * vh / 576), (hi_s32)(w * vw / 720), (hi_s32)(h * vh / 576) };
	if (p->HI_UNF_VO_SetWindowAttr(p->window, &attr.v) != HI_SUCCESS)
		hal_info("hisi live: window %d,%d %dx%d not set\n", r.x, r.y, r.width, r.height);
}

bool cHisiLive::Capture(const std::function<bool(const Frame &)> &use)
{
	std::lock_guard<std::mutex> lock(p->mutex);
	if (!p->video_running)
		return false;
	filled<hi_video_frame_info> info;
	hi_video_frame_info &f = info.v;
	if (p->HI_UNF_VO_CapturePicture(p->window, &f) != HI_SUCCESS)
		return false;
	bool ok = false;
	const hi_video_frame_addr &a = f.addr[0];
	if (f.video_format == HI_UNF_FORMAT_YUV_SEMIPLANAR_420 || f.video_format == HI_UNF_FORMAT_YUV_SEMIPLANAR_420_UV)
	{
		void *y = p->mmz_map ? p->mmz_map(a.y_addr, 0) : NULL;
		void *c = p->mmz_map ? p->mmz_map(a.c_addr, 0) : NULL;
		if (y && c)
		{
			Frame frame = { (const uint8_t *)y, (int)a.y_stride, (const uint8_t *)c, (int)a.c_stride,
				(int)f.width, (int)f.height, f.video_format == HI_UNF_FORMAT_YUV_SEMIPLANAR_420_UV };
			ok = use(frame);
		}
		if (c)
			p->HI_MMZ_Unmap(a.c_addr);
		if (y)
			p->HI_MMZ_Unmap(a.y_addr);
	}
	p->HI_UNF_VO_CapturePictureRelease(p->window, &f);
	return ok;
}

void cHisiLive::Close()
{
	StopAudio();
	Hide();
	std::lock_guard<std::mutex> lock(p->mutex);
	if (!p->avplay)
		return;
	p->HI_UNF_SND_Detach(p->track, p->avplay);
	p->HI_UNF_SND_DestroyTrack(p->track);
	p->HI_UNF_VO_DetachWindow(p->window, p->avplay);
	p->HI_UNF_VO_DestroyWindow(p->window);
	p->HI_UNF_AVPLAY_ChnClose(p->avplay, HI_UNF_AVPLAY_MEDIA_CHAN_VID | HI_UNF_AVPLAY_MEDIA_CHAN_AUD);
	p->HI_UNF_AVPLAY_Destroy(p->avplay);
	p->HI_UNF_DMX_DetachTSPort(p->dmx);
	unlink(HISI_DEMUX_FILE);
	p->avplay = 0;
}
