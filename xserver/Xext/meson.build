srcs_xext = [
    'bigreq.c',
    'geext.c',
    'shape.c',
    'sleepuntil.c',
    'sync.c',
    'xcmisc.c',
    'xtest.c',
]

hdrs_xext = [
    'geext.h',
    'geint.h',
    'syncsdk.h',
]

if build_dpms
    srcs_xext += 'dpms.c'
endif

if build_mitshm
    srcs_xext += 'shm.c'
    hdrs_xext += ['shmint.h']
endif

if build_hashtable
    srcs_xext += 'hashtable.c'
endif

if build_res
    srcs_xext += 'xres.c'
endif

if build_xace
    srcs_xext += 'xace.c'
endif

if build_xinerama
    srcs_xext += ['panoramiX.c', 'panoramiXprocs.c', 'panoramiXSwap.c']
    hdrs_xext += ['panoramiX.h', 'panoramiXsrv.h']
endif

libxserver_xext = static_library('libxserver_xext',
    srcs_xext,
    include_directories: inc,
    dependencies: common_dep,
)

# Vidmode extension is purged in X12
libxserver_xext_vidmode = []

if build_xorg
    install_data(hdrs_xext, install_dir: xorgsdkdir)
endif
