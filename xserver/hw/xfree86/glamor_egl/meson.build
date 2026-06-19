glamoregl_src = [
    '../../../glamor/glamor_egl.c',
    '../../../glamor/glamor_eglmodule.c',
]

if build_xv
    glamoregl_src += 'glamor_xf86_xv.c'
endif

shared_module(
    'glamoregl',
    glamoregl_src,

    include_directories: [inc, xorg_inc],
    c_args: xorg_c_args,
    dependencies: [
        common_dep,
        dependency('libdrm', version: '>= 2.4.46'),
        gbm_dep,
    ],
    link_with: [glamor, libxserver_glx],

    install: true,
    install_dir: module_dir,
)
