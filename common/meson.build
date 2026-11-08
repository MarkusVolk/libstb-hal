common_inc = [config_inc, hal_inc]
if boxtype != 'generic'
	common_inc += include_directories('../libdvbci')
endif

common_src = [
	have_ci ? 'ca_ci.cpp' : 'ca.cpp',
	'hal_debug.cpp',
	'proc_tools.c',
	'pwrmngr.cpp',
	'streaminput.c',
	'version_hal.cpp',
]
common_deps = [threads_dep]
if have_widevine
	common_src += 'widevine/widevine.cpp'
	common_deps += [curl_dep, ffmpeg_deps, meson.get_compiler('cpp').find_library('dl', required : false)]
endif

libcommon = static_library('common',
	common_src,
	include_directories : common_inc,
	dependencies : common_deps,
	pic : true,
)
