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
boxmodel = get_option('boxmodel')
box_models = {
	'generic' : ['generic'],
	'armbox' : ['hd60', 'hd61', 'multibox', 'multiboxse', 'hd51', 'bre2ze4k', 'h7', 'e4hdultra', 'protek4k',
		'osmini4k', 'osmio4k', 'osmio4kplus', 'vusolo4k', 'vuduo4k', 'vuduo4kse', 'vuultimo4k',
		'vuuno4k', 'vuuno4kse', 'vuzero4k'],
	'mipsbox' : ['vuduo', 'vuduo2', 'gb800se', 'osnino', 'osninoplus', 'osninopro'],
}
if boxtype != 'generic' and boxmodel == 'generic'
	error('boxtype @0@ needs a boxmodel, one of: @1@'.format(boxtype, ', '.join(box_models[boxtype])))
endif
if boxmodel not in box_models[boxtype]
	error('boxmodel @0@ does not belong to boxtype @1@'.format(boxmodel, boxtype))
endif
box_groups = {
	'VUPLUS_ARM' : ['vusolo4k', 'vuduo4k', 'vuduo4kse', 'vuultimo4k', 'vuuno4k', 'vuuno4kse', 'vuzero4k'],
	'VUPLUS_MIPS' : ['vuduo', 'vuduo2'],
	'HISILICON' : ['hd60', 'hd61', 'multibox', 'multiboxse'],
}
box_groups += {'VUPLUS_ALL' : box_groups['VUPLUS_ARM'] + box_groups['VUPLUS_MIPS']}
have_ci = boxtype != 'generic' and boxmodel not in ['hd60', 'multibox', 'multiboxse', 'osmini4k', 'osmio4k', 'osmio4kplus']
box_defines = {
	'generic' : 'HAVE_GENERIC_HARDWARE',
	'armbox' : 'HAVE_ARM_HARDWARE',
	'mipsbox' : 'HAVE_MIPS_HARDWARE',
}[boxtype]
box_defines = [box_defines, 'BOXMODEL_' + boxmodel.to_upper()]
foreach group, models : box_groups
	if boxmodel in models
		box_defines += 'BOXMODEL_' + group
	endif
endforeach

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
foreach define : box_defines
	conf.set(define, 1)
endforeach
conf.set('ENABLE_FLV2MPEG4', get_option('flv2mpeg4') ? 1 : false)
conf.set('DEBUG', get_option('debug') ? 1 : false)
conf.set_quoted('NEUTRINO_MPV_CONF', get_option('prefix') / get_option('sysconfdir') / 'neutrino/mpv.conf')
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
ffmpeg_deps = [
	dependency('libavformat', version : '>= 53.21.1'),
	dependency('libavcodec', version : '>= 54.28.0'),
	dependency('libavutil'),
	dependency('libswresample'),
	dependency('libswscale'),
]

hal_parts = []
subdir('common')
hal_parts += libcommon
if boxtype == 'generic'
	subdir('libgeneric-pc')
	hal_parts += libgeneric
else
	if have_ci
		subdir('libdvbci')
		hal_parts += libdvbci
	endif
	subdir('libeplayer3')
	subdir('libarmbox')
	if boxtype == 'mipsbox'
		subdir('libmipsbox')
	endif
	hal_parts += [libbox, libeplayer3]
endif
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
	link_whole : hal_parts,
	version : '0.0.0',
	soversion : '0',
	install : true,
)

pkg = import('pkgconfig')
pkg.generate(libstb_hal,
	name : 'libstb-hal',
	description : 'Hardware abstraction of Neutrino',
	subdirs : 'libstb-hal',
	variables : ['boxtype=' + boxtype, 'boxmodel=' + boxmodel, 'boxdefines=' + ','.join(box_defines)],
)
