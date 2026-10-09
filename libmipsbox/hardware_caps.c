/*
 * determine the capabilities of the hardware.
 * part of libstb-hal
 *
 * (C) 2010-2012 Stefan Seyfried
 *
 * License: GPL v2 or later
 */

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <hardware_caps.h>
#include "video_std.h"

static int initialized = 0;
static hw_caps_t caps;

hw_caps_t *get_hwcaps(void)
{
	if (initialized)
		return &caps;

	memset(&caps, 0, sizeof(hw_caps_t));
	caps.video_std_mask = VIDEO_STD_BIT(VIDEO_STD_PAL) | VIDEO_STD_BIT(VIDEO_STD_576P) |
		VIDEO_STD_BIT(VIDEO_STD_720P50) | VIDEO_STD_BIT(VIDEO_STD_720P60) |
		VIDEO_STD_BIT(VIDEO_STD_1080I50) | VIDEO_STD_BIT(VIDEO_STD_1080I60) |
		VIDEO_STD_BIT(VIDEO_STD_1080P24) | VIDEO_STD_BIT(VIDEO_STD_1080P25) | VIDEO_STD_BIT(VIDEO_STD_1080P50);
	caps.video_std_default = VIDEO_STD_720P50;
	caps.osd_default_height = 720;
	caps.can_psi = 1;
	caps.can_dd_passthrough = 1;
	caps.can_zapping_mode = 1;
	caps.can_ci_clock = 1;
	caps.ci_clock_max = 7;
	caps.can_record_bufsize = 1;
	strcpy(caps.rc_device, "/dev/input/event1");
	strcpy(caps.rc_device_fallback, "/dev/input/event0");
	caps.rc_scan_evdev = 1;
	caps.rc_e2_keys = 1;
	strcpy(caps.display_dev, "/dev/dbox/oled0");
	caps.has_internal_mmc = 1;
	caps.can_ofgwrite = 1;

	caps.pip_devs = 0;
	if (access("/dev/dvb/adapter0/video1", F_OK) != -1)
		caps.pip_devs = 1;
	if (access("/dev/dvb/adapter0/video2", F_OK) != -1)
		caps.pip_devs = 2;
	if (access("/dev/dvb/adapter0/video3", F_OK) != -1)
		caps.pip_devs = 3;
	if (caps.pip_devs > 0)
		caps.can_pip = 1;

#if BOXMODEL_VUDUO
	caps.has_CI = 2;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 16;
	caps.display_type = HW_DISPLAY_LINE_TEXT;
	caps.display_can_deepstandby = 1;
	caps.display_can_set_brightness = 1;
	caps.display_can_umlauts = 0; /* need test */
	caps.display_has_statusline = 0;
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_HDMI = 1;
	caps.has_SCART = 1;
//	caps.has_SCART_input = 1;
	strcpy(caps.startup_file, "");
	strcpy(caps.boxmodel, "vuduo");
	strcpy(caps.boxvendor, "VU+");
	strcpy(caps.boxname, "DUO");
	strcpy(caps.boxarch, "BCM7335");
	caps.can_ci_delay = 1;
	caps.ci_clock_max = 12;
	caps.can_ci_rpr = 1;
	caps.rc_has_playpause = 1;
	caps.rc_has_separate_play = 1;
	caps.tuner_needs_setup_menu = 1;
	caps.nim_socket_vuplus_format = 1;
#endif

	initialized = 1;
	return &caps;
}
