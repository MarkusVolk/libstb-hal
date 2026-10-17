project('libstb-hal', 'c', 'cpp',
	version : '2026.9.0',
	license : 'GPL-2.0-only',
	meson_version : '>= 1.1.0',
	default_options : [
		'warning_level=1',
		'cpp_rtti=false',
		'cpp_eh=none',
		'cpp_std=gnu++23',
	],
)

boxtype = get_option('boxtype')
if boxtype != 'generic'
	error('the Meson build supports boxtype=generic only, use autotools for @0@'.format(boxtype))
endif

version = meson.project_version().split('.')

git = find_program('git', required : false)
git_version = ''
if git.found()
	describe = run_command(git, '-C', meson.project_source_root(), 'describe', '--always', '--tags', '--dirty', check : false)
	branch = run_command(git, '-C', meson.project_source_root(), 'rev-parse', '--abbrev-ref', 'HEAD', check : false)
	if describe.returncode() == 0 and branch.returncode() == 0
		git_version = '@0@ @1@'.format(describe.stdout().strip(), branch.stdout().strip())
	endif
endif

conf = configuration_data()
conf.set('_GNU_SOURCE', 1)
conf.set('HAVE_GENERIC_HARDWARE', 1)
conf.set('BOXMODEL_GENERIC', 1)
conf.set('DEBUG', get_option('debug') ? 1 : false)
conf.set_quoted('PACKAGE_NAME', 'Tuxbox-libstb-hal')
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('PACKAGE_STRING', 'Tuxbox-libstb-hal ' + meson.project_version())
conf.set('PACKAGE_VERSION_MAJOR', version[0])
conf.set('PACKAGE_VERSION_MINOR', version[1])
conf.set('PACKAGE_VERSION_MICRO', version[2])
conf.set_quoted('PACKAGE_VERSION_GIT', git_version)
configure_file(output : 'config.h', configuration : conf)
configure_file(output : 'libstb-hal-config.h', configuration : conf)

add_project_arguments('-fno-strict-aliasing', language : ['c', 'cpp'])

config_inc = include_directories('.')
hal_inc = include_directories('include')

threads_dep = dependency('threads')
rt_dep = meson.get_compiler('c').find_library('rt', required : false)

subdir('common')
subdir('libgeneric-pc')
subdir('tools')

install_headers(
	'include/audio_hal.h',
	'include/ca.h',
	'include/ca_ci.h',
	'include/ca_hal.h',
	'include/cs_api.h',
	'include/cs_frontpanel.h',
	'include/cs_types.h',
	'include/dmx_hal.h',
	'include/glfb.h',
	'include/hal_debug.h',
	'include/hardware_caps.h',
	'include/init.h',
	'include/mmi.h',
	'include/playback_hal.h',
	'include/pwrmngr.h',
	'include/record_hal.h',
	'include/streaminput.h',
	'include/streaminput_ffmpeg.h',
	'include/version_hal.h',
	'include/video_hal.h',
	subdir : 'libstb-hal',
)

libstb_hal = shared_library('stb-hal',
	link_whole : [libcommon, libgeneric],
	version : '0.0.0',
	soversion : '0',
	install : true,
)

pkg = import('pkgconfig')
pkg.generate(libstb_hal,
	name : 'libstb-hal',
	description : 'Hardware abstraction of Neutrino',
	subdirs : 'libstb-hal',
)
