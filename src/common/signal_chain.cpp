// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <dlfcn.h>
#if defined(__OHOS__)
#include <cstring>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "common/assert.h"
#include "common/dynamic_library.h"
#include "common/scope_exit.h"
#include "common/signal_chain.h"

namespace Common {

template <typename T>
T* LookupLibcSymbol(const char* name) {
#if defined(__BIONIC__)
    Common::DynamicLibrary provider("libc.so");
    if (!provider.IsOpen()) {
        std::unreachable(); // Failed to open libc!
    }
#else
    // For other operating environments, we assume the symbol is not overridden.
    const char* base = nullptr;
    Common::DynamicLibrary provider(base);
#endif

    void* sym = provider.GetSymbolAddress(name);
    if (sym == nullptr) {
        sym = dlsym(RTLD_DEFAULT, name);
    }
    if (sym == nullptr) {
        std::unreachable(); // Unable to find symbol {}! (name)
    }

    return reinterpret_cast<T*>(sym);
}

#if defined(__OHOS__)
// 鸿蒙的 musl 会在 sigaction 外面套一层 signal chain 包装器，该包装器用
// pthread_getspecific 取用户处理器。NCE 执行 guest 代码时 tpidr_el0 是 guest TLS，
// 包装器一读就二次崩溃（信号被屏蔽后内核直接杀进程），NCE 自己的处理器永远收不到。
// 这里绕过 libc，直接用 rt_sigaction 系统调用注册。
struct KernelSigAction {
    void* handler;
    unsigned long flags;
    void* restorer;
    unsigned long mask;
};

int SigAction(int signum, const struct sigaction* act, struct sigaction* oldact) {
    KernelSigAction kact{};
    KernelSigAction kold{};
    if (act) {
        kact.handler = (act->sa_flags & SA_SIGINFO) != 0
                           ? reinterpret_cast<void*>(act->sa_sigaction)
                           : reinterpret_cast<void*>(act->sa_handler);
        kact.flags = static_cast<unsigned long>(act->sa_flags) & ~static_cast<unsigned long>(SA_RESTORER);
        kact.restorer = reinterpret_cast<void*>(act->sa_restorer);
        std::memcpy(&kact.mask, &act->sa_mask, sizeof(kact.mask));
    }
    const int ret = static_cast<int>(
        syscall(SYS_rt_sigaction, signum, act ? &kact : nullptr, oldact ? &kold : nullptr, 8));
    if (ret == 0 && oldact) {
        auto& o = *oldact;
        if ((kold.flags & SA_SIGINFO) != 0) {
            o.sa_sigaction = reinterpret_cast<decltype(o.sa_sigaction)>(kold.handler);
        } else {
            o.sa_handler = reinterpret_cast<decltype(o.sa_handler)>(kold.handler);
        }
        o.sa_flags = static_cast<int>(kold.flags);
        o.sa_restorer = reinterpret_cast<decltype(o.sa_restorer)>(kold.restorer);
        std::memcpy(&o.sa_mask, &kold.mask, sizeof(kold.mask));
    }
    return ret;
}
#else
int SigAction(int signum, const struct sigaction* act, struct sigaction* oldact) {
    static auto libc_sigaction = LookupLibcSymbol<decltype(sigaction)>("sigaction");
    return libc_sigaction(signum, act, oldact);
}
#endif

} // namespace Common
