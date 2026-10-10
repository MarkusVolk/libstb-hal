noinst_LTLIBRARIES = libgeneric.la

libgenericpcincludedir = $(includedir)/libgeneric-pc
libgenericpcinclude_HEADERS = \
	audio_lib.h \
	dmx_file.h \
	glfb_priv.h \
	hdmi_cec.h \
	hdmi_cec_types.h \
	linux-uapi-cec.h \
	mpv_player.h \
	playback_lib.h \
	record_lib.h \
	video_lib.h \
	video_std.h

AM_CPPFLAGS = -D__STDC_FORMAT_MACROS -D__STDC_CONSTANT_MACROS
AM_CPPFLAGS += -Wfatal-errors
AM_CPPFLAGS += \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/include \
	@AVUTIL_CFLAGS@ \
	@SDL3_CFLAGS@ \
	@GLES2_CFLAGS@ \
	@MPV_CFLAGS@ \
	@DRM_CFLAGS@

AM_CXXFLAGS = -fno-rtti -fno-exceptions -fno-strict-aliasing

libgeneric_la_LIBADD = \
	-lrt \
	-lOpenThreads \
	@AVFORMAT_LIBS@ \
	@AVUTIL_LIBS@ \
	@AVCODEC_LIBS@ \
	@SWRESAMPLE_LIBS@ \
	@SWSCALE_LIBS@ \
	@SDL3_LIBS@ \
	@GLES2_LIBS@ \
	@MPV_LIBS@ \
	@DRM_LIBS@

libgeneric_la_SOURCES = \
	hardware_caps.c \
	dmx.cpp \
	dmx_file.cpp \
	video.cpp \
	audio.cpp \
	init.cpp \
	record.cpp \
	glfb.cpp \
	hdmi_cec.cpp \
	mpv_player.cpp \
	playback_mpv.cpp
