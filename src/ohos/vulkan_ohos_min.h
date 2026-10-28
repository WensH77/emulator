// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// 精简版 OHOS Vulkan surface 定义：避免与 CPM 拉取的 Khronos 头文件冲突
#pragma once

#include <vulkan/vulkan_core.h>

#define VK_OHOS_SURFACE_EXTENSION_NAME "VK_OHOS_surface"

#ifndef VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS
#define VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS ((VkStructureType)1000685000)
#endif

typedef VkFlags VkSurfaceCreateFlagsOHOS;

struct OHNativeWindow;

typedef struct VkSurfaceCreateInfoOHOS {
    VkStructureType sType;
    const void* pNext;
    VkSurfaceCreateFlagsOHOS flags;
    OHNativeWindow* window;
} VkSurfaceCreateInfoOHOS;

typedef VkResult(VKAPI_PTR* PFN_vkCreateSurfaceOHOS)(
    VkInstance instance, const VkSurfaceCreateInfoOHOS* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
