common_inc = [config_inc, hal_inc]
if boxtype != 'generic'
	common_inc += include_directories('../libdvbci')
endif

libcommon = static_library('common',
	have_ci ? 'ca_ci.cpp' : 'ca.cpp',
	'hal_debug.cpp',
	'proc_tools.c',
	'pwrmngr.cpp',
	'streaminput.c',
	'version_hal.cpp',
	include_directories : common_inc,
	dependencies : threads_dep,
	pic : true,
)
