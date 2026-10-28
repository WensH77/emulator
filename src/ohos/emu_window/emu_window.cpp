// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ohos/emu_window/emu_window.h"

EmuWindow_OHOS::EmuWindow_OHOS() {
    window_info.type = Core::Frontend::WindowSystemType::Ohos;
    window_info.render_surface = nullptr;
    window_info.render_surface_scale = 1.0f;
}

void EmuWindow_OHOS::SetWindow(void* window, u32 width, u32 height) {
    width_ = width ? width : 1;
    height_ = height ? height : 1;
    window_info.type = Core::Frontend::WindowSystemType::Ohos;
    window_info.render_surface = window;
    UpdateCurrentFramebufferLayout(width_, height_);
}
