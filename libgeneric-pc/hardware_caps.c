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
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hardware_caps.h>
#include "video_std.h"
#include <sys/utsname.h>
#include "platform.h"

static int initialized = 0;
static hw_caps_t caps;

/* A line display as the auxdisplay drivers of the kernel make them: the text
 * goes into its attribute "message". Up to four characters are the number
 * display of a set-top box. */
static void probe_display(void)
{
	glob_t g;
	if (glob("/sys/bus/*/devices/*/linedisp.*/message", 0, NULL, &g))
		return;
	const char *message = g.gl_pathv[0];
	if (strlen(message) < sizeof(caps.display_dev))
	{
		char path[sizeof(caps.display_dev) + 8];
		int dir = strrchr(message, '/') - message;
		int chars = 0;
		strcpy(caps.display_dev, message);
		snprintf(path, sizeof(path), "%.*s/num_chars", dir, message);
		FILE *f = fopen(path, "r");
		if (f)
		{
			if (fscanf(f, "%d", &chars) == 1 && chars > 0)
				caps.display_xres = chars;
			fclose(f);
		}
		if (caps.display_xres <= 4)
		{
			caps.display_type = HW_DISPLAY_LED_NUM;
			caps.display_has_channel_number = 1;
		}
		/* the driver of the display says so where it shows "hh:mm" with a colon */
		snprintf(path, sizeof(path), "%.*s/../colon", dir, message);
		caps.display_has_colon = !access(path, F_OK);
	}
	globfree(&g);
}

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
	caps.can_ar_16_10 = 1;
	caps.can_select_audio_output = 1;
	caps.can_live_pause = 1;
	caps.media_in_home = 1;
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
	probe_display();
	platform_caps(&caps);

	initialized = 1;
	return &caps;
}
