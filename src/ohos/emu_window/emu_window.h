// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>

#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"

namespace Core::Frontend {
class GraphicsContext;
}

class GraphicsContext_OHOS final : public Core::Frontend::GraphicsContext {
public:
    GraphicsContext_OHOS() = default;
    ~GraphicsContext_OHOS() override = default;
};

class EmuWindow_OHOS final : public Core::Frontend::EmuWindow {
public:
    EmuWindow_OHOS();
    ~EmuWindow_OHOS() override = default;

    /// 更新原生窗口（OHNativeWindow*）与尺寸
    void SetWindow(void* window, u32 width, u32 height);

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<GraphicsContext_OHOS>();
    }

    bool IsShown() const override {
        return true;
    }

    void OnFrameDisplayed() override {}

private:
    u32 width_{0};
    u32 height_{0};
};
