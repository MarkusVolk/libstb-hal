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
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

static int initialized = 0;
static hw_caps_t caps;

/* A line display as the auxdisplay drivers of the kernel make them: the text
 * goes into its attribute "message". Up to four characters are the number
 * display of a set-top box. */
static uint64_t plane_prop(int fd, uint32_t plane, const char *name, int *found)
{
	drmModeObjectProperties *p = drmModeObjectGetProperties(fd, plane, DRM_MODE_OBJECT_PLANE);
	uint64_t value = 0;
	uint32_t i;
	for (i = 0; p && i < p->count_props; i++)
	{
		drmModePropertyRes *r = drmModeGetProperty(fd, p->props[i]);
		if (r && !strcmp(r->name, name))
		{
			value = p->prop_values[i];
			if (found)
				*found = 1;
		}
		drmModeFreeProperty(r);
	}
	drmModeFreeObjectProperties(p);
	return value;
}

/* video planes below the primary one: the second carries the picture in
 * picture, as the GL frontend uses them */
static int video_planes(void)
{
	int most = 0, card;
	for (card = 0; card < 4; card++)
	{
		char dev[32];
		uint64_t primary_z = 0, z[16];
		int n = 0, below = 0, have_primary = 0, fd;
		uint32_t i, f;
		snprintf(dev, sizeof(dev), "/dev/dri/card%d", card);
		fd = open(dev, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
		drmModePlaneRes *res = drmModeGetPlaneResources(fd);
		for (i = 0; res && i < res->count_planes; i++)
		{
			drmModePlane *p = drmModeGetPlane(fd, res->planes[i]);
			int has_z = 0;
			uint64_t type, zpos;
			if (!p)
				continue;
			type = plane_prop(fd, p->plane_id, "type", NULL);
			zpos = plane_prop(fd, p->plane_id, "zpos", &has_z);
			if (has_z && type == DRM_PLANE_TYPE_PRIMARY)
			{
				primary_z = zpos;
				have_primary = 1;
			}
			else if (has_z && type == DRM_PLANE_TYPE_OVERLAY && n < 16)
				for (f = 0; f < p->count_formats; f++)
					if (p->formats[f] == DRM_FORMAT_NV12)
					{
						z[n++] = zpos;
						break;
					}
			drmModeFreePlane(p);
		}
		drmModeFreePlaneResources(res);
		close(fd);
		for (i = 0; have_primary && i < (uint32_t)n; i++)
			below += z[i] < primary_z;
		if (below > most)
			most = below;
	}
	return most;
}

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

	caps.can_cpufreq = 0;
	/* a desktop shuts down and reboots itself, the box does it through neutrino */
	caps.windowed = getenv("WAYLAND_DISPLAY") || getenv("DISPLAY");
	/* the picture in picture needs a plane of its own for now */
	if (!caps.windowed && video_planes() >= 2)
	{
		caps.can_pip = 1;
		caps.pip_devs = 1;
	}
	caps.can_shutdown = !caps.windowed;
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
#if defined(__x86_64__) || defined(__i386__)
	caps.foreign_disks = 1;
#endif
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
