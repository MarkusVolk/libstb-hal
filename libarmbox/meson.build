box_headers = files(
	'audio_lib.h',
	'hdmi_cec.h',
	'hdmi_cec_types.h',
	'linux-uapi-cec.h',
	'playback_libeplayer3.h',
	'record_lib.h',
	'video_lib.h',
	'video_std.h',
)
box_sources = files(
	'audio.cpp',
	'dmx.cpp',
	'dvb_player.cpp',
	'hdmi_cec.cpp',
	'init.cpp',
	'playback_libeplayer3.cpp',
	'record.cpp',
	'video.cpp',
)
box_args = ['-D__STDC_FORMAT_MACROS', '-D__STDC_CONSTANT_MACROS', '-D_FILE_OFFSET_BITS=64']
box_inc = [config_inc, hal_inc, include_directories('../common', '.'), eplayer3_inc_public]
box_deps = [threads_dep, rt_dep, dependency('openthreads'), ffmpeg_deps]

if boxtype == 'armbox'
	libbox = static_library('armbox',
		box_sources,
		'hardware_caps.c',
		c_args : box_args,
		cpp_args : box_args,
		include_directories : box_inc,
		dependencies : box_deps,
		pic : true,
	)
	install_headers(box_headers, subdir : 'libarmbox')
endif
