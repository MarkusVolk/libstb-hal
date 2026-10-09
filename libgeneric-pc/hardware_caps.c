/*
 * determine the capabilities of the hardware.
 * part of libstb-hal
 *
 * (C) 2010-2012,2016 Stefan Seyfried
 *
 * License: GPL v2 or later
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hardware_caps.h>
#include "video_std.h"
#include <sys/utsname.h>

static int initialized = 0;
static hw_caps_t caps;

hw_caps_t *get_hwcaps(void)
{
	struct utsname u;
	if (initialized)
		return &caps;

	memset(&caps, 0, sizeof(hw_caps_t));

	if (access("/dev/dvb/adapter0/video1", F_OK) != -1)
		caps.can_pip = 1;

	caps.can_cpufreq = 0;
	/* a desktop shuts down and reboots itself, the box does it through neutrino */
	caps.can_shutdown = !(getenv("WAYLAND_DISPLAY") || getenv("DISPLAY"));
	caps.display_type = HW_DISPLAY_LINE_TEXT;
	caps.has_HDMI = 1;
	caps.can_cec = (access("/dev/cec0", F_OK) != -1);
	caps.video_std_mask = VIDEO_STD_BIT(VIDEO_STD_NTSC) | VIDEO_STD_BIT(VIDEO_STD_PAL) |
		VIDEO_STD_BIT(VIDEO_STD_720P50) | VIDEO_STD_BIT(VIDEO_STD_720P60) | VIDEO_STD_BIT(VIDEO_STD_1080I50) |
		VIDEO_STD_BIT(VIDEO_STD_1080P50) | VIDEO_STD_BIT(VIDEO_STD_1080P60);
	caps.video_std_default = VIDEO_STD_720P50;
	caps.osd_default_height = 720;
	caps.can_osd_1080 = 1;
	caps.can_ar_21_9 = 1;
	caps.can_select_audio_output = 1;
	caps.can_live_pause = 1;
	strcpy(caps.rc_device, "/tmp/neutrino.input");
	strcpy(caps.rc_device_fallback, "/tmp/neutrino.input");
	strcpy(caps.display_dev, "/dev/null");
	caps.display_xres = 8;
	caps.display_can_deepstandby = 0;
	caps.display_can_umlauts = 0; /* need test */
	caps.display_has_statusline = 0;
	caps.display_has_colon = 0;
	strcpy(caps.startup_file, "");
	strcpy(caps.boxmodel, "generic");
	strcpy(caps.boxvendor, "Generic");
	strcpy(caps.boxname, "PC");
	if (! uname(&u))
	{
		strncpy(caps.boxarch, u.machine, sizeof(caps.boxarch));
		caps.boxarch[sizeof(caps.boxarch) - 1] = '\0';
	}
	else
		fprintf(stderr, "%s: uname() failed: %m\n", __func__);

	initialized = 1;
	return &caps;
}
