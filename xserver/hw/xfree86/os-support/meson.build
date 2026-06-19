srcs_xorg_os_support = [
    'bus/nobus.c',
    'shared/posix_tty.c',
    'shared/sigio.c',
    'shared/vidmem.c',
]

hdrs_xorg_os_support = [
    'bus/xf86Pci.h',
    'xf86_OSlib.h',
    'xf86_OSproc.h'
]

os_dep = []
os_c_args = []

if get_option('pciaccess')
    srcs_xorg_os_support += 'bus/Pci.c'
    if host_machine.system() != 'linux'
        srcs_xorg_os_support += 'bus/bsd_pci.c'
    endif
    if host_machine.cpu_family() in ['sparc', 'sparc64']
        srcs_xorg_os_support += 'bus/Sbus.c'
        install_data('bus/xf86Sbus.h', install_dir: xorgsdkdir)
    endif
endif

if get_option('agp') == 'auto'
    build_agp = cc.has_header('sys/agpio.h') or cc.has_header('sys/agpgart.h') or cc.has_header('linux/agpgart.h')
else
    build_agp = get_option('agp') == 'true'
endif

if host_machine.system() != 'linux'
    error('X12 is Linux-only!')
endif

srcs_xorg_os_support += [
    'linux/lnx_bell.c',
    'linux/lnx_init.c',
    'linux/lnx_kmod.c',
    'linux/lnx_video.c',
    'misc/SlowBcopy.c',
    'shared/VTsw_usl.c',
]
if libdrm_dep.found()
    srcs_xorg_os_support += 'shared/drm_platform.c'
else
    srcs_xorg_os_support += 'shared/platform_noop.c'
endif
if build_agp
    srcs_xorg_os_support += 'linux/lnx_agp.c'
else
    srcs_xorg_os_support += 'shared/agp_noop.c'
endif
if build_systemd_logind
    srcs_xorg_os_support += 'linux/systemd-logind.c'
endif

# this is ugly because the code is also
if build_apm or build_acpi
    srcs_xorg_os_support += 'linux/lnx_apm.c'
    if build_acpi
        srcs_xorg_os_support += 'linux/lnx_acpi.c'
    endif
else
    srcs_xorg_os_support += 'shared/pm_noop.c'
endif

xorg_os_support = static_library('xorg_os_support',
    srcs_xorg_os_support,
    include_directories: [inc, xorg_inc],
    dependencies: [
        common_dep,
        dbus_dep,
        libdrm_dep,
        os_dep,
    ],
    c_args: xorg_c_args + os_c_args,
)

install_data(hdrs_xorg_os_support, install_dir: xorgsdkdir)
