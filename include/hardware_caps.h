/*
 * determine the capabilities of the hardware.
 * part of libstb-hal
 *
 * (C) 2010-2012 Stefan Seyfried
 *
 * License: GPL v2 or later
 */
#ifndef __HARDWARE_CAPS_H__
#define __HARDWARE_CAPS_H__

#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
	HW_DISPLAY_NONE,
	HW_DISPLAY_LED_ONLY,
	HW_DISPLAY_LED_NUM, /* simple 7 segment LED display */
	HW_DISPLAY_LINE_TEXT, /* 1 line text display */
	HW_DISPLAY_GFX
} display_type_t;


typedef struct hw_caps
{
	int has_fan;
	int has_HDMI;
	int has_HDMI_input;
	int has_SCART;
	int has_SCART_input;
	int has_YUV_cinch;
	int can_pip;
	int pip_devs;
	int can_cpufreq;
	int can_shutdown;
	int can_cec;
	int can_ar_14_9; /* video drivers have 14:9 aspect ratio mode */
	int can_ps_14_9; /* video drivers have 14:9 panscan mode */
	int force_tuner_2G; /* force DVB-S2 even though driver may not advertise it */
	display_type_t display_type;
	int display_xres; /* x resolution or chars per line */
	int display_yres;
	int display_can_umlauts;
	int display_can_deepstandby;
	int display_can_set_brightness;
	int display_has_statusline;
	int display_has_colon;
	int has_button_timer;
	int has_button_vformat;
	char startup_file[64];
	char boxmodel[64];
	char boxvendor[64];
	char boxname[64];
	char boxarch[64];
	int boxtype;
	int has_CI;
	/* video and OSD */
	unsigned int video_std_mask; /* bit VIDEO_STD_x set: the mode is available */
	int video_std_default; /* VIDEO_STD_x before the user chose one */
	int osd_default_height; /* 720 or 1080 */
	int can_osd_1080;
	int can_psi; /* brightness, contrast, saturation and tint of the video */
	int can_dd_passthrough; /* AC3 and DTS pass through switches */
	int can_zapping_mode;
	int can_hdmi_colorimetry;
	int can_ar_21_9;
	int can_select_audio_output; /* HDMI, S/PDIF, analog, USB, Bluetooth */
	int can_live_pause; /* live TV can be held in a buffer */
	int video_needs_blank_frame; /* stopFrame() leaves a picture, a black frame is drawn */
	int pip_warmup; /* PiP has to be opened once at start before it works */
	int standby_zappingmode_mute; /* standby needs zapping mode 2 for a blank screen */
	int fb_wait_vsync; /* FBIO_WAITFORVSYNC before framebuffer operations */
	/* CI and recording */
	int can_ci_clock;
	int ci_clock_max; /* MHz the CI slots take */
	int can_ci_delay;
	int can_ci_rpr; /* relevant PIDs routing */
	int can_record_bufsize;
	/* remote control */
	char rc_device[64]; /* where injected keys go */
	char rc_device_fallback[64];
	int rc_scan_evdev; /* neutrino opens the /dev/input devices itself */
	int rc_e2_keys; /* the remote sends the enigma2 key set: KEY_TV2, KEY_SWITCHVIDEOMODE, ... */
	int rc_has_playpause; /* one key for play and pause */
	int rc_has_separate_play; /* a play key next to play/pause */
	int rc_tvradio_combined; /* one key toggles between TV and radio */
	/* display */
	char display_dev[64];
	int display_has_channel_number;
	int display_can_mirror_video;
	int display_scroll_speed; /* 0: neutrino's default */
	/* system */
	int has_internal_mmc; /* mmcblk is the box's own flash, not a drive */
	int can_ofgwrite; /* images are flashed with ofgwrite */
	int can_boxmode; /* boxmode= on the kernel command line */
	int tuner_needs_setup_menu;
	int tuner_voltage_off_at_init; /* the LNB voltage goes off after the diseqc setup */
	int nim_socket_vuplus_format; /* /proc/bus/nim_sockets names the tuner the VU+ way */
	int multiboot_first_partition; /* mmcblk0p<n> is the first image slot, 0: no such layout */
	int multiboot_devicetree; /* the slot is read from the device tree */
} hw_caps_t;

hw_caps_t *get_hwcaps(void);

#ifdef __cplusplus
}
#endif

#endif // __HARDWARE_CAPS_H__
