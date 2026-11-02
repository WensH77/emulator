// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>

#include "common/settings.h"
#include "core/ohos_arena.h"

#if defined(__OHOS__)
#include <sys/mman.h>
#endif
#include "core/device_memory.h"
#include "hle/kernel/board/nintendo/nx/k_system_control.h"

namespace Core {

#if defined(__OHOS__)
// 只有 NCE 直映射需要把 39 位 guest 地址放进预留区，才需要 160 GiB；
// Dynarmic 走页表翻译，guest 物理内存只有 4 GiB，预留 8 GiB 足够，
// 也避免设备内存紧张时 160 GiB mmap 直接失败（实测会抛 std::bad_alloc）。
size_t OhosArenaSizeBytes() {
    if (Settings::values.cpu_backend.GetValue() != Settings::CpuBackend::Nce) {
        return (1ULL << 33); // 8 GiB：Dynarmic 走页表翻译，guest 物理内存只有 4 GiB
    }

    // NCE 需要把 39 位 guest 地址放进预留区。39 位布局从基址起约占 144 GiB
    // （kmap 64 + stack 2 + alias 64 + heap 12），但鸿蒙在内存紧张时会拒绝大块预留，
    // 这里探测出当前设备实际能给出的最大值，页表再按这个预算收窄区域尺寸。
    static size_t cached_arena_size = 0;
    if (cached_arena_size != 0) {
        return cached_arena_size;
    }
    constexpr size_t GiB = 1ULL << 30;
    constexpr std::array<size_t, 9> candidates{148 * GiB, 144 * GiB, 128 * GiB, 112 * GiB,
                                               96 * GiB,  80 * GiB,  64 * GiB,  48 * GiB,
                                               32 * GiB};
    for (const size_t size : candidates) {
        void* const probe = mmap(nullptr, size, PROT_NONE,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (probe != MAP_FAILED) {
            munmap(probe, size);
            cached_arena_size = size;
            return cached_arena_size;
        }
    }
    // 连 32 GiB 都拿不到：仍然返回最小值，让上层给出明确的失败信息。
    cached_arena_size = 32 * GiB;
    return cached_arena_size;
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
