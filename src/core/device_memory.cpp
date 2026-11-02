// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/ohos_arena.h"
#include "core/device_memory.h"
#include "hle/kernel/board/nintendo/nx/k_system_control.h"

namespace Core {

#if defined(__OHOS__)
// 只有 NCE 直映射需要把 39 位 guest 地址放进预留区，才需要 160 GiB；
// Dynarmic 走页表翻译，guest 物理内存只有 4 GiB，预留 8 GiB 足够，
// 也避免设备内存紧张时 160 GiB mmap 直接失败（实测会抛 std::bad_alloc）。
size_t OhosArenaSizeBytes() {
    if (Settings::values.cpu_backend.GetValue() == Settings::CpuBackend::Nce) {
        // 160 GiB 在当前鸿蒙版本上会被内核拒绝（ENOMEM）；39 位布局从基址起实际占
        // 约 144 GiB（kmap 64 + stack 2 + alias 64 + heap 12），故取 148 GiB。
        return 0x2500000000ULL; // 148 GiB，覆盖 39 位布局约 144 GiB 的占用
    }
    return (1ULL << 33);
}
static size_t OhosVirtualReserveSize() {
    return OhosArenaSizeBytes();
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
