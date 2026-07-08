#ifndef X12_VULKAN_H
#define X12_VULKAN_H

#include <X11/Xfuncproto.h>
#include "scrnintstr.h"
#include "screenint.h"

struct gbm_bo;

// Initialize Vulkan acceleration for a screen
extern _X_EXPORT Bool x12_vulkan_init(ScreenPtr screen);
// Terminate Vulkan acceleration for a screen
extern _X_EXPORT void x12_vulkan_fini(ScreenPtr screen);
// Check if Vulkan acceleration is enabled
extern _X_EXPORT Bool x12_vulkan_is_enabled(ScreenPtr screen);
// Import a GBM buffer object into Vulkan
extern _X_EXPORT Bool x12_vulkan_import_gbm_bo(ScreenPtr screen, struct gbm_bo *bo);
// Import a GBM buffer object into a specific Pixmap's Vulkan private storage
extern _X_EXPORT Bool x12_vulkan_import_gbm_bo_to_pixmap(PixmapPtr pixmap, struct gbm_bo *bo);
// Copy damaged regions from a source pixmap to a destination pixmap via Vulkan
extern _X_EXPORT void x12_vulkan_copy_damage(PixmapPtr dst, PixmapPtr src, RegionPtr dmg, int dx, int dy);

#endif // X12_VULKAN_H
