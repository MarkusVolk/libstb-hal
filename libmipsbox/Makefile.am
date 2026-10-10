noinst_LTLIBRARIES = libmipsbox.la

libmipsboxincludedir = $(includedir)/libmipsbox
libmipsboxinclude_HEADERS = \
	audio_lib.h \
	hdmi_cec.h \
	hdmi_cec_types.h \
	linux-uapi-cec.h \
	playback_libeplayer3.h \
	record_lib.h \
	video_lib.h \
	video_std.h

AM_CPPFLAGS = -D__STDC_FORMAT_MACROS -D__STDC_CONSTANT_MACROS
AM_CPPFLAGS += \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/libeplayer3/include

AM_CXXFLAGS = -fno-rtti -fno-exceptions -fno-strict-aliasing

libmipsbox_la_LIBADD = \
	-lOpenThreads \
	@AVFORMAT_LIBS@ \
	@AVUTIL_LIBS@ \
	@AVCODEC_LIBS@ \
	@SWRESAMPLE_LIBS@ \
	@SWSCALE_LIBS@ \
	-lpthread -lrt

libmipsbox_la_SOURCES = \
	hardware_caps.c \
	dmx.cpp \
	video.cpp \
	audio.cpp \
	init.cpp \
	record.cpp \
	hdmi_cec.cpp \
	playback_libeplayer3.cpp
