project('cpthreads',
    'c',
    version: '0.0.1',
    license: 'MIT'
)

cc = meson.get_compiler('c')

cpthreads = static_library('cpthreads',
    'cpthreads.c',
    name_suffix: 'lib',
    name_prefix: ''
)

cpthreads_shared = shared_library('cpthreads',
    'cpthreads.c'
)

cpthreads_dep = declare_dependency(
    include_directories: include_directories(['.']),
    link_with: cpthreads_shared
)

subdir('tests')