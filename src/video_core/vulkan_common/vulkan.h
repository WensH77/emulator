// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#elif defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(__OHOS__)
// OHOS: 通过 SDK 的 VK_OHOS_surface 扩展创建 surface，不定义桌面平台宏
#else
#define VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif

#include <vulkan/vulkan.h>

// Sanitize macros
#undef CreateEvent
#undef CreateSemaphore
#undef Always
#undef False
#undef None
#undef True
#undef Success
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
