eplayer3_sources = files(
	'container/container.c',
	'container/container_ffmpeg.c',
	'external/ffmpeg/src/bitstream.c',
	'external/ffmpeg/src/latmenc.c',
	'external/ffmpeg/src/mpeg4audio.c',
	'external/ffmpeg/src/xiph.c',
	'external/plugins/src/png.c',
	'manager/audio.c',
	'manager/chapter.c',
	'manager/manager.c',
	'manager/subtitle.c',
	'manager/video.c',
	'output/graphic_subtitle.c',
	'output/graphic_window.c',
	'output/linuxdvb_buffering.c',
	'output/linuxdvb_mipsel.c',
	'output/output.c',
	'output/output_subtitle.c',
	'output/writer/common/misc.c',
	'output/writer/common/pes.c',
	'output/writer/common/writer.c',
	'output/writer/mipsel/aac.c',
	'output/writer/mipsel/ac3.c',
	'output/writer/mipsel/amr.c',
	'output/writer/mipsel/bcma.c',
	'output/writer/mipsel/divx3.c',
	'output/writer/mipsel/dts.c',
	'output/writer/mipsel/h264.c',
	'output/writer/mipsel/h265.c',
	'output/writer/mipsel/lpcm.c',
	'output/writer/mipsel/mjpeg.c',
	'output/writer/mipsel/mp3.c',
	'output/writer/mipsel/mpeg2.c',
	'output/writer/mipsel/mpeg4.c',
	'output/writer/mipsel/pcm.c',
	'output/writer/mipsel/vc1.c',
	'output/writer/mipsel/vp.c',
	'output/writer/mipsel/wmv.c',
	'output/writer/mipsel/writer.c',
	'playback/playback.c',
)
eplayer3_args = ['-D_LARGEFILE_SOURCE', '-D_FILE_OFFSET_BITS=64', '-D_LARGEFILE64_SOURCE']
eplayer3_inc = [config_inc, hal_inc, include_directories('include', 'external')]
if get_option('flv2mpeg4')
	eplayer3_sources += files(
		'external/flv2mpeg4/src/dcprediction.c',
		'external/flv2mpeg4/src/flv2mpeg4.c',
		'external/flv2mpeg4/src/flvdecoder.c',
		'external/flv2mpeg4/src/m4vencode.c',
	)
	eplayer3_args += '-DHAVE_FLV2MPEG4_CONVERTER'
	eplayer3_inc += include_directories('external/flv2mpeg4')
endif

libeplayer3 = static_library('eplayer3',
	eplayer3_sources,
	c_args : eplayer3_args,
	include_directories : eplayer3_inc,
	dependencies : [threads_dep, ffmpeg_deps, meson.get_compiler('c').find_library('dl', required : false)],
	pic : true,
)
eplayer3_inc_public = include_directories('include')
