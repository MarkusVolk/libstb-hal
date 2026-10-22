libbox = static_library('mipsbox',
	box_sources,
	'hardware_caps.c',
	c_args : box_args,
	cpp_args : box_args,
	include_directories : box_inc,
	dependencies : box_deps,
	pic : true,
)
install_headers(box_headers, subdir : 'libmipsbox')
