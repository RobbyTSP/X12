srcs_vulkan = [
    'vk_init.c',
]

libxserver_vulkan = static_library('libxserver_vulkan',
    srcs_vulkan,
    include_directories: inc,
    dependencies: [common_dep, vulkan_dep, gbm_dep],
    c_args: '-DHAVE_DIX_CONFIG_H',
)
