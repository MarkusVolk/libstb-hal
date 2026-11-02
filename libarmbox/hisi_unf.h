/*
 * The part of the HiSilicon HI_UNF API of the Hi3798MV200 libraries that
 * live TV uses: written after the layout of the SDK (HiSTBLinux
 * V100R005C00), with the sizes the SPC060 libraries of the HD60 write.
 * Their stream and status info are 8 bytes longer than the SDK's. Only the
 * fields that are used are named.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __HISI_UNF_H__
#define __HISI_UNF_H__

#include <stddef.h>
#include <stdint.h>

typedef int32_t hi_s32;
typedef uint32_t hi_u32;
typedef uint32_t hi_handle;
typedef int32_t hi_bool;

#define HI_SUCCESS 0

/* avplay */
#define HI_UNF_AVPLAY_STREAM_TYPE_TS 0
#define HI_UNF_AVPLAY_MEDIA_CHAN_AUD 0x1
#define HI_UNF_AVPLAY_MEDIA_CHAN_VID 0x2
#define HI_UNF_AVPLAY_ATTR_ID_ADEC 1
#define HI_UNF_AVPLAY_ATTR_ID_VDEC 2
#define HI_UNF_AVPLAY_ATTR_ID_AUD_PID 3
#define HI_UNF_AVPLAY_ATTR_ID_VID_PID 4
#define HI_UNF_AVPLAY_ATTR_ID_PCR_PID 5
#define HI_UNF_AVPLAY_ATTR_ID_SYNC 6
#define HI_UNF_AVPLAY_STOP_MODE_STILL 0
#define HI_UNF_AVPLAY_STOP_MODE_BLACK 1
#define HI_UNF_SYNC_REF_NONE 0
#define HI_UNF_SYNC_REF_AUDIO 1
#define HI_UNF_SYNC_REF_PCR 3

#define HI_UNF_VCODEC_TYPE_MPEG2 0
#define HI_UNF_VCODEC_TYPE_MPEG4 1
#define HI_UNF_VCODEC_TYPE_AVS 2
#define HI_UNF_VCODEC_TYPE_H264 4
#define HI_UNF_VCODEC_TYPE_VC1 7
#define HI_UNF_VCODEC_TYPE_HEVC 36
#define HI_UNF_VCODEC_MODE_NORMAL 0

/* audio codecs (HA_CODEC_ID_E) */
#define HA_AUDIO_ID_MP2 0x20000002
#define HA_AUDIO_ID_MP3 0x21f00003
#define HA_AUDIO_ID_AAC 0x80020001
/* libHA.AUDIO.MULTI.decode.so, the vendor's FFmpeg decoder of AC3, E-AC3 and DTS */
#define HA_AUDIO_ID_CUSTOM_1 0x81f00401
#define HD_DEC_MODE_RAWPCM 0
#define HA_AUDIO_MAXCHANNELS 8

/* display, video output, demux, sound */
#define HI_UNF_DISPLAY1 1
#define HI_UNF_VO_DEV_MODE_NORMAL 0
#define HI_UNF_VO_ASPECT_CVRS_IGNORE 0
#define HI_LAYER_ZORDER_MOVETOP 0
#define HI_UNF_WINDOW_FREEZE_MODE_BLACK 1
#define HI_UNF_DMX_PORT_TSI_0 0x20
#define HI_UNF_SND_0 0
#define HI_UNF_SND_TRACK_TYPE_MASTER 0
#define HI_UNF_SND_TRACK_TYPE_SLAVE 1

typedef struct
{
	hi_u32 demux_id;
	hi_u32 stream_type;
	hi_u32 vid_buf_size;
	hi_u32 aud_buf_size;
} hi_avplay_attr;

typedef struct
{
	hi_s32 vid_plus_time;
	hi_s32 vid_negative_time;
	hi_bool smooth_play;
} hi_sync_region;

typedef struct
{
	hi_u32 sync_ref;
	hi_sync_region start_region;
	hi_sync_region novel_region;
	hi_s32 vid_pts_adjust;
	hi_s32 aud_pts_adjust;
	hi_u32 pre_sync_timeout_ms;
	hi_bool quick_output;
} hi_sync_attr;

typedef struct
{
	hi_u32 type;
	hi_u32 ext_attr[2];
	hi_u32 mode;
	hi_u32 err_cover;
	hi_u32 priority;
	hi_bool order_output;
	hi_s32 ctrl_options;
	void *codec_context;
} hi_vcodec_attr;

typedef struct
{
	hi_u32 desired_out_channels;
	hi_bool interleaved;
	hi_u32 bit_per_sample;
	hi_u32 desired_sample_rate;
	hi_u32 channel_mapping[HA_AUDIO_MAXCHANNELS];
} hi_pcm_format;

typedef struct
{
	hi_u32 dec_mode;
	hi_pcm_format pcm_format;
	void *codec_private_data;
	hi_u32 codec_private_data_size;
} hi_hadecode_openparam;

typedef struct
{
	hi_u32 type;
	hi_hadecode_openparam decode_param;
} hi_acodec_attr;

typedef struct
{
	hi_s32 x, y, width, height;
} hi_rect;

typedef struct
{
	hi_u32 disp;
	hi_bool is_virtual;
	hi_u32 video_format;
	hi_u32 aspect_cvrs;
	hi_bool user_def_aspect_ratio;
	hi_u32 user_aspect_width;
	hi_u32 user_aspect_height;
	hi_bool use_crop_rect;
	hi_u32 crop_rect[4];
	hi_rect input_rect;
	hi_rect output_rect;
} hi_window_attr;

typedef struct
{
	hi_u32 mode;
	hi_u32 timeout_ms;
} hi_avplay_stop_opt;

typedef struct
{
	hi_u32 first_aud_pts;
	hi_u32 first_vid_pts;
	hi_u32 last_aud_pts;
	hi_u32 last_vid_pts;
	hi_s32 diff_av_play_time;
	hi_u32 play_time;
	hi_u32 local_time;
	hi_u32 rest[61];
} hi_avplay_status_info;

typedef struct
{
	hi_u32 codec_type;
	hi_u32 sub_standard;
	hi_u32 sub_version;
	hi_u32 profile;
	hi_u32 level;
	hi_u32 display_norm;
	hi_bool progressive;
	hi_u32 aspect_width;
	hi_u32 aspect_height;
	hi_u32 bps;
	hi_u32 fps_integer;
	hi_u32 fps_decimal;
	hi_u32 width;
	hi_u32 height;
	hi_u32 display_width;
	hi_u32 display_height;
	hi_u32 display_center_x;
	hi_u32 display_center_y;
	hi_u32 audio_info[7];
} hi_avplay_stream_info;

typedef struct
{
	hi_u32 if_port_num;
	hi_u32 tsi_port_num;
	hi_u32 tso_port_num;
	hi_u32 ram_port_num;
	hi_u32 dmx_num;
	hi_u32 rest[7];
} hi_dmx_capability;

/* opaque, filled by the library's defaults */
typedef struct { hi_u32 data[6]; } hi_audiotrack_attr;
typedef struct { hi_u32 data[148]; } hi_snd_attr;

#define HISI_LAYOUT(t, size) static_assert(sizeof(t) == size, #t " does not match the SDK layout")
HISI_LAYOUT(hi_avplay_attr, 16);
HISI_LAYOUT(hi_sync_attr, 44);
static_assert(offsetof(hi_sync_attr, start_region.smooth_play) == 12, "sync start region");
static_assert(offsetof(hi_sync_attr, novel_region.smooth_play) == 24, "sync novel region");
static_assert(offsetof(hi_sync_attr, quick_output) == 40, "sync quick output");
HISI_LAYOUT(hi_vcodec_attr, 36);
static_assert(offsetof(hi_vcodec_attr, mode) == 12 && offsetof(hi_vcodec_attr, priority) == 20, "vcodec");
HISI_LAYOUT(hi_pcm_format, 48);
HISI_LAYOUT(hi_hadecode_openparam, 60);
HISI_LAYOUT(hi_acodec_attr, 64);
HISI_LAYOUT(hi_window_attr, 80);
static_assert(offsetof(hi_window_attr, aspect_cvrs) == 12 && offsetof(hi_window_attr, output_rect) == 64, "window");
HISI_LAYOUT(hi_avplay_stop_opt, 8);
HISI_LAYOUT(hi_avplay_status_info, 272);
HISI_LAYOUT(hi_avplay_stream_info, 100);
static_assert(offsetof(hi_avplay_stream_info, width) == 48 && offsetof(hi_avplay_stream_info, fps_integer) == 40, "stream info");
HISI_LAYOUT(hi_dmx_capability, 48);
HISI_LAYOUT(hi_audiotrack_attr, 24);
HISI_LAYOUT(hi_snd_attr, 592);
#undef HISI_LAYOUT

#endif
