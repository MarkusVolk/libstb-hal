ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libstb-hal.la
libstb_hal_la_SOURCES =
SUBDIRS = common tools

libstb_hal_la_LIBADD = \
	common/libcommon.la

libstbhalincludedir = $(includedir)/libstb-hal
libstbhalinclude_HEADERS = \
	include/audio_hal.h \
	include/ca.h \
	include/ca_ci.h \
	include/ca_hal.h \
	include/cs_api.h \
	include/cs_frontpanel.h \
	include/cs_types.h \
	include/dmx_hal.h \
	include/glfb.h \
	include/hal_debug.h \
	include/hardware_caps.h \
	include/init.h \
	include/mmi.h \
	include/playback_hal.h \
	include/pwrmngr.h \
	include/record_hal.h \
	include/streaminput.h \
	include/streaminput_ffmpeg.h \
	include/version_hal.h \
	include/video_hal.h

#bin_PROGRAMS = libstb-hal-test
#libstb_hal_test_SOURCES = libtest.cpp
#libstb_hal_test_LDADD = libstb-hal.la

# there has to be a better way to do this...
if BOXTYPE_GENERIC
SUBDIRS += libgeneric-pc
libstb_hal_la_LIBADD += \
	libgeneric-pc/libgeneric.la
endif

if BOXTYPE_ARMBOX
if BOXMODEL_HD60
SUBDIRS += libarmbox
libstb_hal_la_LIBADD += \
	libarmbox/libarmbox.la
else
if BOXMODEL_MULTIBOX
SUBDIRS += libarmbox
libstb_hal_la_LIBADD += \
	libarmbox/libarmbox.la
else
if BOXMODEL_MULTIBOXSE
SUBDIRS += libarmbox
libstb_hal_la_LIBADD += \
	libarmbox/libarmbox.la
else
if BOXMODEL_OSMINI4K
SUBDIRS += libarmbox
libstb_hal_la_LIBADD += \
	libarmbox/libarmbox.la
else
if BOXMODEL_OSMIO4K
SUBDIRS += libarmbox
libstb_hal_la_LIBADD += \
	libarmbox/libarmbox.la
else
if BOXMODEL_OSMIO4KPLUS
SUBDIRS += libarmbox
libstb_hal_la_LIBADD += \
	libarmbox/libarmbox.la
else
#libstb_hal_test_LDADD += -lasound
SUBDIRS += libarmbox libdvbci
libstb_hal_la_LIBADD += \
	libarmbox/libarmbox.la \
	libdvbci/libdvbci.la
endif
endif
endif
endif
endif
endif
SUBDIRS += libeplayer3
libstb_hal_la_LIBADD += \
	libeplayer3/libeplayer3.la
endif

if BOXTYPE_MIPSBOX
SUBDIRS += libmipsbox libdvbci
libstb_hal_la_LIBADD += \
	libmipsbox/libmipsbox.la \
	libdvbci/libdvbci.la

SUBDIRS += libeplayer3
libstb_hal_la_LIBADD += \
	libeplayer3/libeplayer3.la
endif
