// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/device_memory.h"
#include "hle/kernel/board/nintendo/nx/k_system_control.h"

namespace Core {

#if defined(__OHOS__)
// 单次 mmap 预留 256 GiB 在鸿蒙上会 OOM，先用 160 GiB；
// 分段预留方案见 CitronOhosProbeReserve 的探测结果
constexpr size_t VirtualReserveSize = (1ULL << 37) + (1ULL << 35);
#elif defined(HAS_NCE)
constexpr size_t VirtualReserveSize = 1ULL << 38;
#else
constexpr size_t VirtualReserveSize = 1ULL << 39;
#endif

DeviceMemory::DeviceMemory(std::span<const int> backing_fds)
    : buffer{Kernel::Board::Nintendo::Nx::KSystemControl::Init::GetIntendedMemorySize(),
             VirtualReserveSize, backing_fds, GuestMemoryChunkSize} {}

DeviceMemory::~DeviceMemory() = default;

} // namespace Core
