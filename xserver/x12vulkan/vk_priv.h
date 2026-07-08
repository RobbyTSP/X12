#ifndef X12_VK_PRIV_H
#define X12_VK_PRIV_H

#include <vulkan/vulkan.h>
#include "privates.h"
#include "x12vulkan.h"

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkCommandPool command_pool;
    VkImage imported_image;
    VkDeviceMemory imported_memory;
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    void *staging_mapped;
    VkDeviceSize staging_size;
    Bool initialized;
} X12VulkanContext;

typedef struct {
    CreatePixmapProcPtr CreatePixmap;
    DestroyPixmapProcPtr DestroyPixmap;
} X12VulkanScreenPrivate;

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    VkFormat format;
    Bool is_vulkan;
} X12VulkanPixmapPrivate;

extern X12VulkanContext g_vk_ctx;
extern DevPrivateKeyRec x12_vulkan_screen_private_key;
extern DevPrivateKeyRec x12_vulkan_pixmap_private_key;

#endif // X12_VK_PRIV_H
