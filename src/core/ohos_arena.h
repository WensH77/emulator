// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>

namespace Core {

/// 鸿蒙上 DeviceMemory 实际预留的虚拟地址空间大小（字节）。
/// NCE 直映射要求 guest 地址落在这段范围内，页表裁剪必须用同一个值。
size_t OhosArenaSizeBytes();

} // namespace Core
