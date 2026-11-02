// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/device_memory.h"
#include "hle/kernel/board/nintendo/nx/k_system_control.h"

namespace Core {

#if defined(__OHOS__)
// 只有 NCE 直映射需要把 39 位 guest 地址放进预留区，才需要 160 GiB；
// Dynarmic 走页表翻译，guest 物理内存只有 4 GiB，预留 8 GiB 足够，
// 也避免设备内存紧张时 160 GiB mmap 直接失败（实测会抛 std::bad_alloc）。
static size_t OhosVirtualReserveSize() {
    if (Settings::values.cpu_backend.GetValue() == Settings::CpuBackend::Nce) {
        return (1ULL << 37) + (1ULL << 35); // 160 GiB
    }
    return (1ULL << 33); // 8 GiB
}
[[maybe_unused]] constexpr size_t VirtualReserveSize = 0;
#elif defined(HAS_NCE)
constexpr size_t VirtualReserveSize = 1ULL << 38;
#else
constexpr size_t VirtualReserveSize = 1ULL << 39;
#endif

DeviceMemory::DeviceMemory(std::span<const int> backing_fds)
    : buffer{Kernel::Board::Nintendo::Nx::KSystemControl::Init::GetIntendedMemorySize(),
#if defined(__OHOS__)
             OhosVirtualReserveSize(),
#else
             VirtualReserveSize,
#endif
             backing_fds, GuestMemoryChunkSize} {}

DeviceMemory::~DeviceMemory() = default;

} // namespace Core
