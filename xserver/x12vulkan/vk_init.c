#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <gbm.h>
#include "os.h"
#include "pixmapstr.h"
#include "regionstr.h"
#include "vk_priv.h"

DevPrivateKeyRec x12_vulkan_screen_private_key;
DevPrivateKeyRec x12_vulkan_pixmap_private_key;

X12VulkanContext g_vk_ctx = {
    .instance = VK_NULL_HANDLE,
    .physical_device = VK_NULL_HANDLE,
    .device = VK_NULL_HANDLE,
    .graphics_queue = VK_NULL_HANDLE,
    .command_pool = VK_NULL_HANDLE,
    .imported_image = VK_NULL_HANDLE,
    .imported_memory = VK_NULL_HANDLE,
    .staging_buffer = VK_NULL_HANDLE,
    .staging_memory = VK_NULL_HANDLE,
    .staging_mapped = NULL,
    .staging_size = 0,
    .initialized = FALSE
};

static Bool
create_instance(void)
{
    const char *extension_names[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    };

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "X12 Server",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "X12 Vulkan Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = extension_names,
    };

    if (vkCreateInstance(&create_info, NULL, &g_vk_ctx.instance) != VK_SUCCESS) {
        return FALSE;
    }

    return TRUE;
}

static Bool
pick_physical_device(void)
{
    uint32_t device_count = 0;
    if (vkEnumeratePhysicalDevices(g_vk_ctx.instance, &device_count, NULL) != VK_SUCCESS || device_count == 0) {
        return FALSE;
    }

    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * device_count);
    if (!devices)
        return FALSE;

    if (vkEnumeratePhysicalDevices(g_vk_ctx.instance, &device_count, devices) != VK_SUCCESS) {
        free(devices);
        return FALSE;
    }

    g_vk_ctx.physical_device = devices[0];
    free(devices);

    return TRUE;
}

static Bool
create_device(void)
{
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char *extension_names[] = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    };

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = extension_names,
    };

    if (vkCreateDevice(g_vk_ctx.physical_device, &create_info, NULL, &g_vk_ctx.device) != VK_SUCCESS) {
        return FALSE;
    }

    vkGetDeviceQueue(g_vk_ctx.device, 0, 0, &g_vk_ctx.graphics_queue);

    return TRUE;
}

static Bool
create_command_pool(void)
{
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };

    if (vkCreateCommandPool(g_vk_ctx.device, &pool_info, NULL, &g_vk_ctx.command_pool) != VK_SUCCESS) {
        return FALSE;
    }

    return TRUE;
}

static Bool
create_staging_buffer(VkDeviceSize size)
{
    // Clean up old staging buffer if it exists
    if (g_vk_ctx.staging_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.staging_buffer, NULL);
        g_vk_ctx.staging_buffer = VK_NULL_HANDLE;
    }
    if (g_vk_ctx.staging_memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_vk_ctx.device, g_vk_ctx.staging_memory, NULL);
        g_vk_ctx.staging_memory = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(g_vk_ctx.device, &buffer_info, NULL, &g_vk_ctx.staging_buffer) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to create staging buffer\n");
        return FALSE;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, g_vk_ctx.staging_buffer, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(g_vk_ctx.physical_device, &mem_properties);
    uint32_t mem_type_index = (uint32_t)-1;
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1 << i))) {
            if ((mem_properties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                mem_type_index = i;
                break;
            }
        }
    }

    if (mem_type_index == (uint32_t)-1) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to find suitable memory type for staging buffer\n");
        vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.staging_buffer, NULL);
        g_vk_ctx.staging_buffer = VK_NULL_HANDLE;
        return FALSE;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = mem_type_index,
    };

    if (vkAllocateMemory(g_vk_ctx.device, &alloc_info, NULL, &g_vk_ctx.staging_memory) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to allocate staging memory\n");
        vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.staging_buffer, NULL);
        g_vk_ctx.staging_buffer = VK_NULL_HANDLE;
        return FALSE;
    }

    if (vkBindBufferMemory(g_vk_ctx.device, g_vk_ctx.staging_buffer, g_vk_ctx.staging_memory, 0) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to bind staging buffer memory\n");
        vkFreeMemory(g_vk_ctx.device, g_vk_ctx.staging_memory, NULL);
        vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.staging_buffer, NULL);
        g_vk_ctx.staging_buffer = VK_NULL_HANDLE;
        g_vk_ctx.staging_memory = VK_NULL_HANDLE;
        return FALSE;
    }

    if (vkMapMemory(g_vk_ctx.device, g_vk_ctx.staging_memory, 0, size, 0, &g_vk_ctx.staging_mapped) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to map staging memory\n");
        vkFreeMemory(g_vk_ctx.device, g_vk_ctx.staging_memory, NULL);
        vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.staging_buffer, NULL);
        g_vk_ctx.staging_buffer = VK_NULL_HANDLE;
        g_vk_ctx.staging_memory = VK_NULL_HANDLE;
        return FALSE;
    }

    g_vk_ctx.staging_size = size;
    LogMessage(X_INFO, "[X12 Vulkan] Allocated host-visible staging buffer of size %llu bytes\n", (unsigned long long)size);
    return TRUE;
}

Bool
x12_vulkan_import_gbm_bo(ScreenPtr screen, struct gbm_bo *bo)
{
    if (!g_vk_ctx.initialized)
        return FALSE;

    // 1. Get DMA-BUF file descriptor and properties from GBM BO
    int fd = gbm_bo_get_fd(bo);
    if (fd < 0) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to get DMA-BUF FD from GBM BO\n");
        return FALSE;
    }

    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);

    off_t size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    LogMessage(X_INFO, "[X12 Vulkan] Importing GBM BO: width=%u, height=%u, size=%lld, fd=%d\n",
               width, height, (long long)size, fd);

    // 2. Cleanup old imported resources if they exist
    if (g_vk_ctx.imported_image != VK_NULL_HANDLE) {
        vkDestroyImage(g_vk_ctx.device, g_vk_ctx.imported_image, NULL);
        g_vk_ctx.imported_image = VK_NULL_HANDLE;
    }
    if (g_vk_ctx.imported_memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_vk_ctx.device, g_vk_ctx.imported_memory, NULL);
        g_vk_ctx.imported_memory = VK_NULL_HANDLE;
    }

    // 3. Create VkImage with External Memory
    VkExternalMemoryImageCreateInfo external_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &external_info,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM, // Standard 32-bit BGRA format
        .extent = {
            .width = width,
            .height = height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(g_vk_ctx.device, &image_info, NULL, &g_vk_ctx.imported_image) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to create external VkImage\n");
        close(fd);
        return FALSE;
    }

    // 4. Get Memory Requirements
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(g_vk_ctx.device, g_vk_ctx.imported_image, &mem_reqs);

    // 5. Find memory type index
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(g_vk_ctx.physical_device, &mem_properties);
    uint32_t mem_type_index = (uint32_t)-1;
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1 << i))) {
            mem_type_index = i;
            break;
        }
    }

    if (mem_type_index == (uint32_t)-1) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to find suitable memory type for import\n");
        vkDestroyImage(g_vk_ctx.device, g_vk_ctx.imported_image, NULL);
        g_vk_ctx.imported_image = VK_NULL_HANDLE;
        close(fd);
        return FALSE;
    }

    // 6. Allocate/Import Device Memory
    VkImportMemoryFdInfoKHR import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd = fd,
    };

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_info,
        .allocationSize = size > 0 ? (VkDeviceSize)size : mem_reqs.size,
        .memoryTypeIndex = mem_type_index,
    };

    // vkAllocateMemory takes ownership of fd on KHR_external_memory_fd specification
    if (vkAllocateMemory(g_vk_ctx.device, &alloc_info, NULL, &g_vk_ctx.imported_memory) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to import VkDeviceMemory\n");
        vkDestroyImage(g_vk_ctx.device, g_vk_ctx.imported_image, NULL);
        g_vk_ctx.imported_image = VK_NULL_HANDLE;
        close(fd); // Close since ownership was not taken
        return FALSE;
    }

    // 7. Bind Image to Imported Memory
    if (vkBindImageMemory(g_vk_ctx.device, g_vk_ctx.imported_image, g_vk_ctx.imported_memory, 0) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to bind VkImage to imported VkDeviceMemory\n");
        vkFreeMemory(g_vk_ctx.device, g_vk_ctx.imported_memory, NULL);
        vkDestroyImage(g_vk_ctx.device, g_vk_ctx.imported_image, NULL);
        g_vk_ctx.imported_image = VK_NULL_HANDLE;
        g_vk_ctx.imported_memory = VK_NULL_HANDLE;
        return FALSE;
    }

    LogMessage(X_INFO, "[X12 Vulkan] Successfully imported GBM BO to Vulkan Image\n");

    if (!create_staging_buffer((VkDeviceSize)width * height * 4)) {
        LogMessage(X_ERROR, "[X12 Vulkan] Staging buffer allocation failed during import\n");
        return FALSE;
    }

    // Clear the imported image to verify rendering functionality
    {
        VkCommandBufferAllocateInfo cmd_alloc_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = g_vk_ctx.command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer cmd;
        if (vkAllocateCommandBuffers(g_vk_ctx.device, &cmd_alloc_info, &cmd) == VK_SUCCESS) {
            VkCommandBufferBeginInfo begin_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };

            if (vkBeginCommandBuffer(cmd, &begin_info) == VK_SUCCESS) {
                // Transition to TRANSFER_DST_OPTIMAL
                VkImageMemoryBarrier barrier1 = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = g_vk_ctx.imported_image,
                    .srcAccessMask = 0,
                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                };

                vkCmdPipelineBarrier(cmd,
                                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0,
                                     0, NULL,
                                     0, NULL,
                                     1, &barrier1);

                // Premium slate-indigo clear color
                VkClearColorValue clear_color = {
                    .float32 = {0.1f, 0.15f, 0.3f, 1.0f}
                };

                VkImageSubresourceRange range = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };

                vkCmdClearColorImage(cmd,
                                     g_vk_ctx.imported_image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     &clear_color,
                                     1, &range);

                // Transition to GENERAL layout
                VkImageMemoryBarrier barrier2 = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = g_vk_ctx.imported_image,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                };

                vkCmdPipelineBarrier(cmd,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                     0,
                                     0, NULL,
                                     0, NULL,
                                     1, &barrier2);

                if (vkEndCommandBuffer(cmd) == VK_SUCCESS) {
                    VkSubmitInfo submit_info = {
                        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1,
                        .pCommandBuffers = &cmd,
                    };

                    if (vkQueueSubmit(g_vk_ctx.graphics_queue, 1, &submit_info, VK_NULL_HANDLE) == VK_SUCCESS) {
                        vkQueueWaitIdle(g_vk_ctx.graphics_queue);
                        LogMessage(X_INFO, "[X12 Vulkan] Render clear test executed successfully\n");
                    } else {
                        LogMessage(X_ERROR, "[X12 Vulkan] Failed to submit render clear commands\n");
                    }
                }
            }
            vkFreeCommandBuffers(g_vk_ctx.device, g_vk_ctx.command_pool, 1, &cmd);
        } else {
            LogMessage(X_ERROR, "[X12 Vulkan] Failed to allocate command buffer for render clear\n");
        }
    }

    return TRUE;
}

static PixmapPtr
x12_vulkan_create_pixmap(ScreenPtr screen, int w, int h, int depth, unsigned int usage)
{
    PixmapPtr pixmap;
    X12VulkanPixmapPrivate *priv;
    X12VulkanScreenPrivate *screen_priv = dixLookupPrivate(&screen->devPrivates, &x12_vulkan_screen_private_key);
    int pitch;
    VkFormat format;

    // Call fallback directly for special usages or invalid sizes
    if (w == 0 || h == 0 || depth == 0 || usage == CREATE_PIXMAP_USAGE_SCRATCH) {
        return (*screen_priv->CreatePixmap)(screen, w, h, depth, usage);
    }

    // Map X11 depth to Vulkan format
    switch (depth) {
    case 15:
        format = VK_FORMAT_A1R5G5B5_UNORM_PACK16;
        break;
    case 16:
        format = VK_FORMAT_R5G6B5_UNORM_PACK16;
        break;
    case 24:
    case 32:
        format = VK_FORMAT_B8G8R8A8_UNORM;
        break;
    default:
        // Unsupported depth, fallback to CPU
        return (*screen_priv->CreatePixmap)(screen, w, h, depth, usage);
    }

    // 1. Allocate a dummy 0x0 Pixmap shell via the fallback CreatePixmap to avoid CPU allocation
    pixmap = (*screen_priv->CreatePixmap)(screen, 0, 0, depth, usage);
    if (!pixmap)
        return NullPixmap;

    priv = dixLookupPrivate(&pixmap->devPrivates, &x12_vulkan_pixmap_private_key);
    if (!priv) {
        (*screen_priv->DestroyPixmap)(pixmap);
        return NullPixmap;
    }

    // Align pitch to 64 bytes for GPU alignment
    int bpp = pixmap->drawable.bitsPerPixel;
    pitch = (((w * bpp + 7) / 8) + 63) & ~63;

    // Modify the header attributes to represent the real dimensions
    screen->ModifyPixmapHeader(pixmap, w, h, 0, 0, pitch, NULL);

    // 2. Create the Vulkan Image
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {
            .width = w,
            .height = h,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(g_vk_ctx.device, &image_info, NULL, &priv->image) != VK_SUCCESS) {
        (*screen_priv->DestroyPixmap)(pixmap);
        // Fallback to CPU allocation
        return (*screen_priv->CreatePixmap)(screen, w, h, depth, usage);
    }

    // 3. Allocate and bind Device Memory
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(g_vk_ctx.device, priv->image, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(g_vk_ctx.physical_device, &mem_properties);
    uint32_t mem_type_index = (uint32_t)-1;
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1 << i))) {
            // Prefer device local memory for optimal rendering performance
            if (mem_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                mem_type_index = i;
                break;
            }
        }
    }
    // Fallback if no device local memory type is found
    if (mem_type_index == (uint32_t)-1) {
        for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
            if ((mem_reqs.memoryTypeBits & (1 << i))) {
                mem_type_index = i;
                break;
            }
        }
    }

    if (mem_type_index == (uint32_t)-1) {
        vkDestroyImage(g_vk_ctx.device, priv->image, NULL);
        (*screen_priv->DestroyPixmap)(pixmap);
        return (*screen_priv->CreatePixmap)(screen, w, h, depth, usage);
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = mem_type_index,
    };

    if (vkAllocateMemory(g_vk_ctx.device, &alloc_info, NULL, &priv->memory) != VK_SUCCESS) {
        vkDestroyImage(g_vk_ctx.device, priv->image, NULL);
        (*screen_priv->DestroyPixmap)(pixmap);
        return (*screen_priv->CreatePixmap)(screen, w, h, depth, usage);
    }

    if (vkBindImageMemory(g_vk_ctx.device, priv->image, priv->memory, 0) != VK_SUCCESS) {
        vkFreeMemory(g_vk_ctx.device, priv->memory, NULL);
        vkDestroyImage(g_vk_ctx.device, priv->image, NULL);
        (*screen_priv->DestroyPixmap)(pixmap);
        return (*screen_priv->CreatePixmap)(screen, w, h, depth, usage);
    }

    priv->width = w;
    priv->height = h;
    priv->pitch = pitch;
    priv->format = format;
    priv->is_vulkan = TRUE;

    return pixmap;
}

static Bool
x12_vulkan_destroy_pixmap(PixmapPtr pixmap)
{
    if (pixmap->refcnt == 1) {
        X12VulkanPixmapPrivate *priv = dixLookupPrivate(&pixmap->devPrivates, &x12_vulkan_pixmap_private_key);
        if (priv && priv->is_vulkan) {
            vkDestroyImage(g_vk_ctx.device, priv->image, NULL);
            vkFreeMemory(g_vk_ctx.device, priv->memory, NULL);
            priv->image = VK_NULL_HANDLE;
            priv->memory = VK_NULL_HANDLE;
            priv->is_vulkan = FALSE;
        }
    }
    ScreenPtr screen = pixmap->drawable.pScreen;
    X12VulkanScreenPrivate *screen_priv = dixLookupPrivate(&screen->devPrivates, &x12_vulkan_screen_private_key);
    return (*screen_priv->DestroyPixmap)(pixmap);
}

Bool
x12_vulkan_import_gbm_bo_to_pixmap(PixmapPtr pixmap, struct gbm_bo *bo)
{
    if (!g_vk_ctx.initialized)
        return FALSE;

    X12VulkanPixmapPrivate *priv = dixLookupPrivate(&pixmap->devPrivates, &x12_vulkan_pixmap_private_key);
    if (!priv)
        return FALSE;

    // Get DMA-BUF file descriptor and properties from GBM BO
    int fd = gbm_bo_get_fd(bo);
    if (fd < 0) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to get DMA-BUF FD from GBM BO for pixmap\n");
        return FALSE;
    }

    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    off_t size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    // Cleanup old resources inside the pixmap private if they exist
    if (priv->is_vulkan) {
        vkDestroyImage(g_vk_ctx.device, priv->image, NULL);
        vkFreeMemory(g_vk_ctx.device, priv->memory, NULL);
        priv->image = VK_NULL_HANDLE;
        priv->memory = VK_NULL_HANDLE;
        priv->is_vulkan = FALSE;
    }

    // Create VkImage with External Memory
    VkExternalMemoryImageCreateInfo external_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &external_info,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM, // Standard 32-bit format
        .extent = {
            .width = width,
            .height = height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(g_vk_ctx.device, &image_info, NULL, &priv->image) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to create external VkImage for pixmap\n");
        close(fd);
        return FALSE;
    }

    // Get Memory Requirements
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(g_vk_ctx.device, priv->image, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(g_vk_ctx.physical_device, &mem_properties);
    uint32_t mem_type_index = (uint32_t)-1;
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1 << i))) {
            mem_type_index = i;
            break;
        }
    }

    if (mem_type_index == (uint32_t)-1) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to find suitable memory type for import\n");
        vkDestroyImage(g_vk_ctx.device, priv->image, NULL);
        close(fd);
        return FALSE;
    }

    VkImportMemoryFdInfoKHR import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd = fd,
    };

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_info,
        .allocationSize = size > 0 ? (VkDeviceSize)size : mem_reqs.size,
        .memoryTypeIndex = mem_type_index,
    };

    if (vkAllocateMemory(g_vk_ctx.device, &alloc_info, NULL, &priv->memory) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to import VkDeviceMemory for pixmap\n");
        vkDestroyImage(g_vk_ctx.device, priv->image, NULL);
        close(fd);
        return FALSE;
    }

    if (vkBindImageMemory(g_vk_ctx.device, priv->image, priv->memory, 0) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to bind VkImage to imported memory for pixmap\n");
        vkFreeMemory(g_vk_ctx.device, priv->memory, NULL);
        vkDestroyImage(g_vk_ctx.device, priv->image, NULL);
        return FALSE;
    }

    priv->width = width;
    priv->height = height;
    priv->pitch = gbm_bo_get_stride(bo);
    priv->format = VK_FORMAT_B8G8R8A8_UNORM;
    priv->is_vulkan = TRUE;

    LogMessage(X_INFO, "[X12 Vulkan] Successfully imported GBM BO to Pixmap VkImage: %p\n", (void*)pixmap);
    return TRUE;
}

void
x12_vulkan_copy_damage(PixmapPtr dst, PixmapPtr src, RegionPtr dmg, int dx, int dy)
{
    if (!g_vk_ctx.initialized)
        return;

    X12VulkanPixmapPrivate *dst_priv = dixLookupPrivate(&dst->devPrivates, &x12_vulkan_pixmap_private_key);
    X12VulkanPixmapPrivate *src_priv = dixLookupPrivate(&src->devPrivates, &x12_vulkan_pixmap_private_key);

    if (!dst_priv || !dst_priv->is_vulkan) {
        LogMessage(X_WARNING, "[X12 Vulkan] Copy damage: destination is not Vulkan-backed\n");
        return;
    }

    int num_rects = RegionNumRects(dmg);
    if (num_rects == 0)
        return;

    BoxPtr rects = RegionRects(dmg);
    Bool src_is_vulkan = (src_priv && src_priv->is_vulkan);

    if (!src_is_vulkan) {
        if (!src->devPrivate.ptr) {
            LogMessage(X_WARNING, "[X12 Vulkan] Copy damage: source is neither Vulkan-backed nor has CPU pixel pointer\n");
            return;
        }
        if (g_vk_ctx.staging_buffer == VK_NULL_HANDLE || !g_vk_ctx.staging_mapped) {
            LogMessage(X_WARNING, "[X12 Vulkan] Copy damage: staging buffer not allocated/mapped\n");
            return;
        }
    }

    VkCommandBufferAllocateInfo cmd_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_vk_ctx.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(g_vk_ctx.device, &cmd_alloc_info, &cmd) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to allocate command buffer for copy damage\n");
        return;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to begin command buffer for copy damage\n");
        vkFreeCommandBuffers(g_vk_ctx.device, g_vk_ctx.command_pool, 1, &cmd);
        return;
    }

    if (src_is_vulkan) {
        // GPU-to-GPU Copy (vkCmdCopyImage)
        VkImageMemoryBarrier barrier_src1 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = src_priv->image,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VkImageMemoryBarrier barrier_dst1 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst_priv->image,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VkImageMemoryBarrier barriers1[2] = { barrier_src1, barrier_dst1 };

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0, NULL,
                             0, NULL,
                             2, barriers1);

        VkImageCopy *copy_regions = malloc(sizeof(VkImageCopy) * num_rects);
        if (!copy_regions) {
            vkFreeCommandBuffers(g_vk_ctx.device, g_vk_ctx.command_pool, 1, &cmd);
            return;
        }

        for (int i = 0; i < num_rects; i++) {
            int x1 = rects[i].x1;
            int y1 = rects[i].y1;
            int x2 = rects[i].x2;
            int y2 = rects[i].y2;

            copy_regions[i] = (VkImageCopy){
                .srcSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .srcOffset = { x1, y1, 0 },
                .dstSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .dstOffset = { x1 + dx, y1 + dy, 0 },
                .extent = { (uint32_t)(x2 - x1), (uint32_t)(y2 - y1), 1 },
            };
        }

        vkCmdCopyImage(cmd,
                       src_priv->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst_priv->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       num_rects, copy_regions);

        free(copy_regions);

        VkImageMemoryBarrier barrier_src2 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = src_priv->image,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VkImageMemoryBarrier barrier_dst2 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst_priv->image,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VkImageMemoryBarrier barriers2[2] = { barrier_src2, barrier_dst2 };

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0, NULL,
                             0, NULL,
                             2, barriers2);
    } else {
        // CPU-to-GPU Copy (vkCmdCopyBufferToImage via staging buffer)
        VkImageMemoryBarrier barrier_dst1 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst_priv->image,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0, NULL,
                             0, NULL,
                             1, &barrier_dst1);

        VkBufferImageCopy *copy_regions = malloc(sizeof(VkBufferImageCopy) * num_rects);
        if (!copy_regions) {
            vkFreeCommandBuffers(g_vk_ctx.device, g_vk_ctx.command_pool, 1, &cmd);
            return;
        }

        uint8_t *src_pixels = src->devPrivate.ptr;
        int src_stride = src->devKind;
        int cpp = (src->drawable.bitsPerPixel + 7) / 8;
        VkDeviceSize current_offset = 0;

        for (int i = 0; i < num_rects; i++) {
            int x1 = rects[i].x1;
            int y1 = rects[i].y1;
            int x2 = rects[i].x2;
            int y2 = rects[i].y2;
            int rect_w = x2 - x1;
            int rect_h = y2 - y1;
            VkDeviceSize rect_size = rect_w * rect_h * cpp;

            if (current_offset + rect_size > g_vk_ctx.staging_size) {
                num_rects = i; // Cut copy to what fits in staging buffer
                break;
            }

            // Copy row-by-row to staging buffer
            uint8_t *dst_ptr = (uint8_t *)g_vk_ctx.staging_mapped + current_offset;
            uint8_t *src_ptr = src_pixels + y1 * src_stride + x1 * cpp;
            for (int r = 0; r < rect_h; r++) {
                memcpy(dst_ptr + r * rect_w * cpp, src_ptr + r * src_stride, rect_w * cpp);
            }

            copy_regions[i] = (VkBufferImageCopy){
                .bufferOffset = current_offset,
                .bufferRowLength = rect_w,
                .bufferImageHeight = rect_h,
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageOffset = { x1 + dx, y1 + dy, 0 },
                .imageExtent = { (uint32_t)rect_w, (uint32_t)rect_h, 1 },
            };

            current_offset += rect_size;
        }

        if (num_rects > 0) {
            vkCmdCopyBufferToImage(cmd,
                                   g_vk_ctx.staging_buffer,
                                   dst_priv->image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   num_rects, copy_regions);
        }

        free(copy_regions);

        VkImageMemoryBarrier barrier_dst2 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst_priv->image,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0, NULL,
                             0, NULL,
                             1, &barrier_dst2);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to end command buffer for copy damage\n");
        vkFreeCommandBuffers(g_vk_ctx.device, g_vk_ctx.command_pool, 1, &cmd);
        return;
    }

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };

    if (vkQueueSubmit(g_vk_ctx.graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to submit copy damage command buffer\n");
    } else {
        vkQueueWaitIdle(g_vk_ctx.graphics_queue);
    }

    vkFreeCommandBuffers(g_vk_ctx.device, g_vk_ctx.command_pool, 1, &cmd);
}



Bool
x12_vulkan_init(ScreenPtr screen)
{
    if (g_vk_ctx.initialized)
        return TRUE;

    LogMessage(X_INFO, "[X12 Vulkan] Initializing Vulkan backend...\n");

    if (!dixPrivateKeyRegistered(&x12_vulkan_screen_private_key)) {
        if (!dixRegisterPrivateKey(&x12_vulkan_screen_private_key, PRIVATE_SCREEN, sizeof(X12VulkanScreenPrivate))) {
            LogMessage(X_ERROR, "[X12 Vulkan] Failed to register screen private key\n");
            return FALSE;
        }
    }
    if (!dixPrivateKeyRegistered(&x12_vulkan_pixmap_private_key)) {
        if (!dixRegisterPrivateKey(&x12_vulkan_pixmap_private_key, PRIVATE_PIXMAP, sizeof(X12VulkanPixmapPrivate))) {
            LogMessage(X_ERROR, "[X12 Vulkan] Failed to register pixmap private key\n");
            return FALSE;
        }
    }

    if (!create_instance()) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to create Vulkan instance\n");
        return FALSE;
    }

    if (!pick_physical_device()) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to pick physical device\n");
        return FALSE;
    }

    if (!create_device()) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to create logical device\n");
        return FALSE;
    }

    if (!create_command_pool()) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to create command pool\n");
        return FALSE;
    }

    X12VulkanScreenPrivate *screen_priv = dixLookupPrivate(&screen->devPrivates, &x12_vulkan_screen_private_key);
    if (!screen_priv) {
        LogMessage(X_ERROR, "[X12 Vulkan] Failed to lookup screen private structure\n");
        return FALSE;
    }
    screen_priv->CreatePixmap = screen->CreatePixmap;
    screen_priv->DestroyPixmap = screen->DestroyPixmap;

    screen->CreatePixmap = x12_vulkan_create_pixmap;
    screen->DestroyPixmap = x12_vulkan_destroy_pixmap;

    g_vk_ctx.initialized = TRUE;
    LogMessage(X_INFO, "[X12 Vulkan] Vulkan backend initialized successfully\n");
    return TRUE;
}

void
x12_vulkan_fini(ScreenPtr screen)
{
    if (!g_vk_ctx.initialized)
        return;

    LogMessage(X_INFO, "[X12 Vulkan] Shutting down Vulkan backend...\n");

    X12VulkanScreenPrivate *screen_priv = dixLookupPrivate(&screen->devPrivates, &x12_vulkan_screen_private_key);
    if (screen_priv) {
        screen->CreatePixmap = screen_priv->CreatePixmap;
        screen->DestroyPixmap = screen_priv->DestroyPixmap;
    }

    if (g_vk_ctx.staging_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.staging_buffer, NULL);
        g_vk_ctx.staging_buffer = VK_NULL_HANDLE;
    }

    if (g_vk_ctx.staging_memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_vk_ctx.device, g_vk_ctx.staging_memory, NULL);
        g_vk_ctx.staging_memory = VK_NULL_HANDLE;
    }

    if (g_vk_ctx.imported_image != VK_NULL_HANDLE) {
        vkDestroyImage(g_vk_ctx.device, g_vk_ctx.imported_image, NULL);
        g_vk_ctx.imported_image = VK_NULL_HANDLE;
    }

    if (g_vk_ctx.imported_memory != VK_NULL_HANDLE) {
        vkFreeMemory(g_vk_ctx.device, g_vk_ctx.imported_memory, NULL);
        g_vk_ctx.imported_memory = VK_NULL_HANDLE;
    }

    if (g_vk_ctx.command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(g_vk_ctx.device, g_vk_ctx.command_pool, NULL);
        g_vk_ctx.command_pool = VK_NULL_HANDLE;
    }

    if (g_vk_ctx.device != VK_NULL_HANDLE) {
        vkDestroyDevice(g_vk_ctx.device, NULL);
        g_vk_ctx.device = VK_NULL_HANDLE;
    }

    if (g_vk_ctx.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(g_vk_ctx.instance, NULL);
        g_vk_ctx.instance = VK_NULL_HANDLE;
    }

    g_vk_ctx.initialized = FALSE;
}

Bool
x12_vulkan_is_enabled(ScreenPtr screen)
{
    return g_vk_ctx.initialized;
}
