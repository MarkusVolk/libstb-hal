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
		VIDEO_STD_BIT(VIDEO_STD_1080P24) | VIDEO_STD_BIT(VIDEO_STD_1080P25) | VIDEO_STD_BIT(VIDEO_STD_1080P50) |
		VIDEO_STD_BIT(VIDEO_STD_2160P24) | VIDEO_STD_BIT(VIDEO_STD_2160P25) |
		VIDEO_STD_BIT(VIDEO_STD_2160P30) | VIDEO_STD_BIT(VIDEO_STD_2160P50);
	caps.video_std_default = VIDEO_STD_1080P50;
	caps.osd_default_height = 1080;
	caps.can_osd_1080 = 1;
	caps.can_psi = 1;
	caps.can_dd_passthrough = 1;
	caps.can_zapping_mode = 1;
	caps.video_needs_blank_frame = 1;
	caps.fb_wait_vsync = 1;
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

#if BOXMODEL_VUSOLO4K
	caps.has_CI = 1;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 480;
	caps.display_yres = 320;
	caps.display_type = HW_DISPLAY_GFX;
	caps.display_can_umlauts = 0; // needs test
	caps.display_can_deepstandby = 0;    // 0 because we use graphlcd
	caps.display_can_set_brightness = 0; // 0 because we use graphlcd
	caps.display_has_statusline = 0;     // 0 because we use graphlcd
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	caps.pip_devs = 2; // has only 3 real usable video devices
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "vusolo4k");
	strcpy(caps.boxvendor, "VU+");
	strcpy(caps.boxname, "SOLO4K");
	strcpy(caps.boxarch, "BCM7376");
	caps.can_hdmi_colorimetry = 1;
	caps.can_ci_delay = 1;
	caps.ci_clock_max = 12;
	caps.can_ci_rpr = 1;
	caps.rc_has_playpause = 1;
	caps.rc_has_separate_play = 1;
	caps.tuner_needs_setup_menu = 1;
	caps.display_can_mirror_video = 1;
	caps.display_scroll_speed = 2;
	caps.nim_socket_vuplus_format = 1;
	caps.multiboot_first_partition = 5;
#endif
#if BOXMODEL_VUDUO4K
	caps.has_CI = 2;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 480;
	caps.display_yres = 320;
	caps.display_type = HW_DISPLAY_GFX;
	caps.display_can_umlauts = 0; // needs test
	caps.display_can_deepstandby = 0;    // 0 because we use graphlcd
	caps.display_can_set_brightness = 0; // 0 because we use graphlcd
	caps.display_has_statusline = 0;     // 0 because we use graphlcd
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 2;
	caps.has_HDMI_input = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "vuduo4k");
	strcpy(caps.boxvendor, "VU+");
	strcpy(caps.boxname, "DUO4K");
	strcpy(caps.boxarch, "BCM7278");
	caps.can_hdmi_colorimetry = 1;
	caps.can_ci_delay = 1;
	caps.ci_clock_max = 12;
	caps.can_ci_rpr = 1;
	caps.rc_has_playpause = 1;
	caps.rc_has_separate_play = 1;
	caps.tuner_needs_setup_menu = 1;
	caps.display_can_mirror_video = 1;
	caps.display_scroll_speed = 2;
	caps.nim_socket_vuplus_format = 1;
	caps.multiboot_first_partition = 10;
#endif
#if BOXMODEL_VUDUO4KSE
	caps.has_CI = 2;
	caps.can_cec = 1;
	caps.can_shutdown = 1;
	caps.display_xres = 480;
	caps.display_yres = 320;
	caps.display_type = HW_DISPLAY_GFX;
	caps.display_can_umlauts = 0; // needs test
	caps.display_can_deepstandby = 0;    // 0 because we use graphlcd
	caps.display_can_set_brightness = 0; // 0 because we use graphlcd
	caps.display_has_statusline = 0;     // 0 because we use graphlcd
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 2;
	caps.has_HDMI_input = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "vuduo4kse");
	strcpy(caps.boxvendor, "VU+");
	strcpy(caps.boxname, "DUO4KSE");
	strcpy(caps.boxarch, "BCM7444S");
	caps.can_hdmi_colorimetry = 1;
	caps.can_ci_delay = 1;
	caps.ci_clock_max = 12;
	caps.can_ci_rpr = 1;
	caps.rc_has_playpause = 1;
	caps.rc_has_separate_play = 1;
	caps.tuner_needs_setup_menu = 1;
	caps.display_can_mirror_video = 1;
	caps.display_scroll_speed = 2;
	caps.nim_socket_vuplus_format = 1;
#endif
#if BOXMODEL_VUULTIMO4K
	caps.has_CI = 2;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 800;
	caps.display_yres = 480;
	caps.display_type = HW_DISPLAY_GFX;
	caps.display_can_umlauts = 0; // needs test
	caps.display_can_deepstandby = 0;    // 0 because we use graphlcd
	caps.display_can_set_brightness = 0; // 0 because we use graphlcd
	caps.display_has_statusline = 0;     // 0 because we use graphlcd
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 2;
	caps.has_HDMI_input = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "vuultimo4k");
	strcpy(caps.boxvendor, "VU+");
	strcpy(caps.boxname, "ULTIMO4K");
	strcpy(caps.boxarch, "BCM7444S");
	caps.can_hdmi_colorimetry = 1;
	caps.can_ci_delay = 1;
	caps.ci_clock_max = 12;
	caps.can_ci_rpr = 1;
	caps.rc_has_playpause = 1;
	caps.rc_has_separate_play = 1;
	caps.tuner_needs_setup_menu = 1;
	caps.display_can_mirror_video = 1;
	caps.display_scroll_speed = 2;
	caps.nim_socket_vuplus_format = 1;
	caps.multiboot_first_partition = 5;
#endif
#if BOXMODEL_VUZERO4K
	caps.has_CI = 1;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_type = HW_DISPLAY_LED_ONLY;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 1;
	caps.display_has_statusline = 0;
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "vuzero4k");
	strcpy(caps.boxvendor, "VU+");
	strcpy(caps.boxname, "ZERO4K");
	strcpy(caps.boxarch, "BCM72604");
	caps.can_hdmi_colorimetry = 1;
	caps.can_ci_delay = 1;
	caps.ci_clock_max = 12;
	caps.can_ci_rpr = 1;
	caps.rc_has_playpause = 1;
	caps.rc_has_separate_play = 1;
	caps.tuner_needs_setup_menu = 1;
	caps.nim_socket_vuplus_format = 1;
	caps.multiboot_first_partition = 8;
#endif
#if BOXMODEL_VUUNO4KSE
	caps.has_CI = 1;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 400;
	caps.display_yres = 240;
	caps.display_type = HW_DISPLAY_GFX;
	caps.display_can_umlauts = 0; // needs test
	caps.display_can_deepstandby = 0;    // 0 because we use graphlcd
	caps.display_can_set_brightness = 0; // 0 because we use graphlcd
	caps.display_has_statusline = 0;     // 0 because we use graphlcd
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 2;
	caps.has_HDMI_input = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "vuuno4kse");
	strcpy(caps.boxvendor, "VU+");
	strcpy(caps.boxname, "UNO4KSE");
	strcpy(caps.boxarch, "BCM7252S");
	caps.can_hdmi_colorimetry = 1;
	caps.can_ci_delay = 1;
	caps.ci_clock_max = 12;
	caps.can_ci_rpr = 1;
	caps.rc_has_playpause = 1;
	caps.rc_has_separate_play = 1;
	caps.tuner_needs_setup_menu = 1;
	caps.display_can_mirror_video = 1;
	caps.display_scroll_speed = 1;
	caps.nim_socket_vuplus_format = 1;
	caps.multiboot_first_partition = 5;
#endif
#if BOXMODEL_VUUNO4K
	caps.has_CI = 1;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_type = HW_DISPLAY_LED_ONLY;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 1;
	caps.display_has_statusline = 0;
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "vuuno4k");
	strcpy(caps.boxvendor, "VU+");
	strcpy(caps.boxname, "UNO4K");
	strcpy(caps.boxarch, "BCM7252S");
	caps.can_hdmi_colorimetry = 1;
	caps.can_ci_delay = 1;
	caps.ci_clock_max = 12;
	caps.can_ci_rpr = 1;
	caps.rc_has_playpause = 1;
	caps.rc_has_separate_play = 1;
	caps.tuner_needs_setup_menu = 1;
	caps.nim_socket_vuplus_format = 1;
	caps.multiboot_first_partition = 5;
#endif
#if BOXMODEL_HD51
	caps.has_CI = 1;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 16;
	caps.display_type = HW_DISPLAY_LINE_TEXT;
	caps.display_can_umlauts = 1;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 1;
	caps.display_has_statusline = 0;
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "hd51");
	strcpy(caps.boxvendor, "AX");
	strcpy(caps.boxname, "HD51");
	strcpy(caps.boxarch, "BCM7251S");
	caps.rc_has_playpause = 1;
	caps.pip_warmup = 1;
	caps.can_boxmode = 1;
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_BRE2ZE4K
	caps.has_CI = 1;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 4;
	caps.display_type = HW_DISPLAY_LED_NUM;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 1;
	caps.display_has_statusline = 0;
	caps.display_has_colon = 1;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 1;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "breeze4k");
	strcpy(caps.boxvendor, "WWIO");
	strcpy(caps.boxname, "BRE2ZE4K");
	strcpy(caps.boxarch, "BCM7251S");
	caps.rc_has_playpause = 1;
	caps.pip_warmup = 1;
	caps.display_has_channel_number = 1;
	caps.can_boxmode = 1;
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_H7
	caps.has_CI = 1;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 4;
	caps.display_type = HW_DISPLAY_LED_NUM;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 1;
	caps.display_has_statusline = 0;
	caps.display_has_colon = 1;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "h7");
	strcpy(caps.boxvendor, "AirDigital");
	strcpy(caps.boxname, "Zgemma H7");
	strcpy(caps.boxarch, "BCM7251S");
	caps.rc_has_playpause = 1;
	caps.pip_warmup = 1;
	caps.display_has_channel_number = 1;
	caps.can_boxmode = 1;
	strcpy(caps.rc_device, "/dev/input/event2");
	strcpy(caps.rc_device_fallback, "/dev/input/event1");
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_E4HDULTRA
	caps.has_CI = 1;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 220;
	caps.display_yres = 176;
	caps.display_type = HW_DISPLAY_GFX;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;    // 0 because we use graphlcd
	caps.display_can_set_brightness = 0; // 0 because we use graphlcd
	caps.display_has_statusline = 0;     // 0 because we use graphlcd
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "e4hdultra");
	strcpy(caps.boxvendor, "AXAS");
	strcpy(caps.boxname, "E4HD 4K ULTRA");
	strcpy(caps.boxarch, "BCM7252S");
	caps.rc_has_playpause = 1;
	caps.pip_warmup = 1;
	caps.standby_zappingmode_mute = 1;
	caps.rc_tvradio_combined = 1;
	strcpy(caps.display_dev, "/dev/null");
	caps.tuner_voltage_off_at_init = 1;
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_PROTEK4K
	caps.has_CI = 1;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 220;
	caps.display_yres = 176;
	caps.display_type = HW_DISPLAY_GFX;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;    // 0 because we use graphlcd
	caps.display_can_set_brightness = 0; // 0 because we use graphlcd
	caps.display_has_statusline = 0;     // 0 because we use graphlcd
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "protek4k");
	strcpy(caps.boxvendor, "Protek");
	strcpy(caps.boxname, "Protek 4K UHD");
	strcpy(caps.boxarch, "BCM7252S");
	caps.rc_has_playpause = 1;
	caps.rc_tvradio_combined = 1;
	strcpy(caps.display_dev, "/dev/null");
	caps.tuner_voltage_off_at_init = 1;
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_HD60
	caps.has_CI = 0;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 4;
	caps.display_type = HW_DISPLAY_LED_NUM;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 1;
	caps.display_has_statusline = 0;
	caps.display_has_colon = 1;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP_LINUX");
	strcpy(caps.boxmodel, "hd60");
	strcpy(caps.boxvendor, "AX");
	strcpy(caps.boxname, "HD60");
	strcpy(caps.boxarch, "HI3798MV200");
	caps.rc_has_playpause = 1;
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_HD61
	caps.has_CI = 2;
	caps.can_cec = 1;
	caps.can_shutdown = 1;
	caps.display_xres = 4;
	caps.display_type = HW_DISPLAY_LED_NUM;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 1;
	caps.display_has_statusline = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP_LINUX");
	strcpy(caps.boxmodel, "hd61");
	strcpy(caps.boxvendor, "AX");
	strcpy(caps.boxname, "HD61");
	strcpy(caps.boxarch, "HI3798MV200");
	caps.rc_has_playpause = 1;
	caps.rc_tvradio_combined = 1;
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_MULTIBOX
	caps.has_CI = 0;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 0;
	caps.display_type = HW_DISPLAY_NONE;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 0;
	caps.display_has_statusline = 0;
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP_LINUX");
	strcpy(caps.boxmodel, "multibox");
	strcpy(caps.boxvendor, "Maxytec");
	strcpy(caps.boxname, "Multibox 4K");
	strcpy(caps.boxarch, "HI3798MV200");
	caps.rc_has_playpause = 1;
	strcpy(caps.display_dev, "/dev/null");
	strcpy(caps.rc_device, "/dev/input/event0");
	strcpy(caps.rc_device_fallback, "/dev/input/event1");
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_MULTIBOXSE
	caps.has_CI = 0;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 0;
	caps.display_type = HW_DISPLAY_NONE;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 0;
	caps.display_has_statusline = 0;
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 0;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP_LINUX");
	strcpy(caps.boxmodel, "multiboxse");
	strcpy(caps.boxvendor, "Maxytec");
	strcpy(caps.boxname, "Multibox SE 4K");
	strcpy(caps.boxarch, "HI3798MV200");
	caps.rc_has_playpause = 1;
	strcpy(caps.display_dev, "/dev/null");
	strcpy(caps.rc_device, "/dev/input/event0");
	strcpy(caps.rc_device_fallback, "/dev/input/event1");
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_OSMINI4K
	caps.has_CI = 0;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 4;
	caps.display_type = HW_DISPLAY_LED_NUM;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 1;
	caps.display_has_statusline = 1;
	caps.display_has_colon = 1;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 1;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "osmini4k");
	strcpy(caps.boxvendor, "Edision");
	strcpy(caps.boxname, "OS mini 4K");
	strcpy(caps.boxarch, "BCM72604");
	caps.rc_has_playpause = 1;
#endif
#if BOXMODEL_OSMIO4K
	caps.has_CI = 0;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 4;
	caps.display_type = HW_DISPLAY_LED_NUM;
	caps.display_can_umlauts = 0;
	caps.display_can_deepstandby = 0;
	caps.display_can_set_brightness = 1;
	caps.display_has_statusline = 1;
	caps.display_has_colon = 1;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 1;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "osmio4k");
	strcpy(caps.boxvendor, "Edision");
	strcpy(caps.boxname, "OS mio 4K");
	strcpy(caps.boxarch, "BCM72604");
	caps.rc_has_playpause = 1;
	strcpy(caps.rc_device, "/dev/input/event0");
	strcpy(caps.rc_device_fallback, "/dev/input/event1");
	caps.video_std_mask |= VIDEO_STD_BIT(VIDEO_STD_1080P60);
	caps.multiboot_devicetree = 1;
#endif
#if BOXMODEL_OSMIO4KPLUS
	caps.has_CI = 0;
	caps.can_cec = 1;
	caps.can_cpufreq = 0;
	caps.can_shutdown = 1;
	caps.display_xres = 128;
	caps.display_yres = 32;
	caps.display_type = HW_DISPLAY_GFX;
	caps.display_can_umlauts = 0;        // needs test
	caps.display_can_deepstandby = 0;    // evaluation is required with usage of graphlcd/lcd4linux, in this case = 0
	caps.display_can_set_brightness = 1; // evaluation is required with usage of graphlcd/lcd4linux, in this case = 0
	caps.display_has_statusline = 0;     // evaluation is required with usage of graphlcd/lcd4linux, in this case = 0
	caps.display_has_colon = 0;
	caps.has_button_timer = 1;
	caps.has_button_vformat = 1;
	caps.has_HDMI = 1;
	strcpy(caps.startup_file, "STARTUP");
	strcpy(caps.boxmodel, "osmio4kplus");
	strcpy(caps.boxvendor, "Edision");
	strcpy(caps.boxname, "OS mio+ 4K");
	strcpy(caps.boxarch, "BCM72604");
	caps.rc_has_playpause = 1;
	strcpy(caps.rc_device, "/dev/input/event0");
	strcpy(caps.rc_device_fallback, "/dev/input/event1");
	caps.video_std_mask |= VIDEO_STD_BIT(VIDEO_STD_1080P60);
	caps.multiboot_devicetree = 1;
#endif

	initialized = 1;
	return &caps;
}
