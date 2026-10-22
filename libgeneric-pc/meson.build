generic_sources = []
generic_deps = []
generic_objects = []
generic_args = ['-D__STDC_FORMAT_MACROS', '-D__STDC_CONSTANT_MACROS', '-Wfatal-errors']
generic_inc = [config_inc, hal_inc, include_directories('../common')]
generic_common_deps = [
	threads_dep,
	rt_dep,
	dependency('openthreads'),
	dependency('libavformat', version : '>= 53.21.1'),
	dependency('libavcodec', version : '>= 54.28.0'),
	dependency('libavutil'),
	dependency('libswresample'),
	dependency('libswscale'),
	dependency('sdl3', version : '>= 3.2.0'),
	dependency('glesv2'),
	dependency('mpv', version : '>= 1.108'),
	dependency('libdrm'),
]
if get_option('webos').enabled()
	webos_protocols = get_option('webos_protocols')
	if webos_protocols == ''
		error('webos needs -Dwebos_protocols, the directory of webos-wayland-extensions')
	endif
	wayland_scanner = find_program('wayland-scanner', native : true)
	foreach proto : ['webos-shell', 'webos-foreign']
		xml = webos_protocols / proto + '.xml'
		generic_sources += custom_target(proto + '-header',
			input : xml,
			output : proto + '-client-protocol.h',
			command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
		)
		generic_sources += custom_target(proto + '-code',
			input : xml,
			output : proto + '-protocol.c',
			command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
		)
	endforeach
	generic_sources += ['webos/platform_webos.cpp', 'webos/glfb_webos.cpp', 'webos/webos_app.cpp', 'webos/webos_input.cpp', 'webos/webos_request.cpp']
	generic_deps += [dependency('wayland-client'), dependency('libplayerAPIs')]
	# LG's media library reports failures as C++ exceptions
	starfish = static_library('starfish',
		'webos/starfish_player.cpp',
		cpp_args : generic_args,
		include_directories : generic_inc,
		dependencies : generic_common_deps + generic_deps,
		override_options : ['cpp_eh=default'],
		pic : true,
	)
	generic_objects += starfish.extract_all_objects(recursive : false)
else
	generic_sources += 'platform.cpp'
endif

libgeneric = static_library('generic',
	generic_sources,
	'hardware_caps.c',
	'dmx.cpp',
	'dmx_file.cpp',
	'video.cpp',
	'audio.cpp',
	'init.cpp',
	'record.cpp',
	'glfb.cpp',
	'hdmi_cec.cpp',
	'mpv_player.cpp',
	'playback_mpv.cpp',
	c_args : generic_args,
	cpp_args : generic_args,
	include_directories : generic_inc,
	dependencies : generic_common_deps + generic_deps,
	objects : generic_objects,
	pic : true,
)

install_headers(
	'audio_lib.h',
	'dmx_file.h',
	'glfb_priv.h',
	'hdmi_cec.h',
	'hdmi_cec_types.h',
	'linux-uapi-cec.h',
	'mpv_player.h',
	'playback_lib.h',
	'record_lib.h',
	'video_lib.h',
	'video_std.h',
	subdir : 'libgeneric-pc',
)
