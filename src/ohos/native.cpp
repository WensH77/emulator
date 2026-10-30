// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ohos/native.h"

#include <cstdlib>
#include <hilog/log.h>
#include <new>

#include <atomic>
#include <csignal>
#include <execinfo.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <cstdio>
#include <cstring>
#include <cstring>
#include <memory>
#include <exception>
#include <filesystem>
#include <map>
#include <chrono>
#include <thread>
#include <csignal>
#include <execinfo.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <cstdio>
#include <cstring>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <optional>
#include <string>

#include "common/logging.h"
#include "common/settings.h"
#include "common/settings.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/cpu_manager.h"
#include "core/hle/kernel/physical_core.h"
#include "core/core_timing.h"
#include "core/crypto/key_manager.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/set/settings.h"
#include "core/hle/service/apm/apm.h"
#include "core/hle/kernel/k_scheduler.h"
#include "core/hle/service/am/event_observer.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/am/applet_manager.h"
#include "ohos/emu_window/emu_window.h"
#include "common/dynamic_library.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/framebuffer_config.h"
#include "video_core/host1x/host1x.h"
#include "core/hle/service/nvnflinger/pixel_format.h"
#include "common/math_util.h"
#include "video_core/video_core.h"

// 临时诊断：记录失败的大块分配
void* operator new(std::size_t size) {
    void* p = std::malloc(size);
    if (!p) {
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD006, "VKCITRON",
                     "operator new FAILED size=%{public}zu", size);
        throw std::bad_alloc();
    }
    return p;
}

void* operator new[](std::size_t size) {
    void* p = std::malloc(size);
    if (!p) {
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD006, "VKCITRON",
                     "operator new[] FAILED size=%{public}zu", size);
        throw std::bad_alloc();
    }
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

std::atomic<int> g_probe_result{-1};
std::atomic<int> g_init_result{-1};

// 崩溃时把信号号写进 stderr（已重定向到沙箱里的日志文件）
static void CrashSignalHandler(int sig, siginfo_t* info, void* context) {
    (void)context;
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "=== CRASH: signal=%d (%s) addr=%p ===\n", sig,
                     strsignal(sig), info ? info->si_addr : nullptr);
    if (n > 0) {
        ssize_t written = write(2, buf, static_cast<size_t>(n));
        (void)written;
    }
    void* frames[32];
    const int frame_count = ::backtrace(frames, 32);
    ::backtrace_symbols_fd(frames, frame_count, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}

// 收到 SIGUSR2 时把当前线程的宿主调用栈写进 stderr（已重定向到沙箱日志）
static int g_dump_fd = -1;

static void DumpThreadHandler(int, siginfo_t*, void*) {
    const int fd = g_dump_fd >= 0 ? g_dump_fd : 2;
    char header[64];
    const int header_len =
        snprintf(header, sizeof(header), "=== 线程栈 tid=%d ===\n", static_cast<int>(gettid()));
    ssize_t written = write(fd, header, static_cast<size_t>(header_len));
    (void)written;
    void* frames[40];
    const int count = ::backtrace(frames, 40);
    ::backtrace_symbols_fd(frames, count, fd);
    const char* tail = "=== 栈结束 ===\n";
    written = write(fd, tail, strlen(tail));
    (void)written;
}

static void InstallDumpHandler() {
    struct sigaction sa{};
    sa.sa_sigaction = DumpThreadHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR2, &sa, nullptr);
}

static void InstallCrashHandlers() {
    struct sigaction sa{};
    sa.sa_sigaction = CrashSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE}) {
        sigaction(sig, &sa, nullptr);
    }
}

std::string g_status = "not started";
std::string g_root;

// 只创建一次，供 TestSurface / TestClear 共用
struct GlobalVk {
    Common::DynamicLibrary library{"libvulkan.so"};
    Vulkan::vk::InstanceDispatch dld{};
    std::optional<Vulkan::vk::Instance> instance;
    std::optional<Vulkan::vk::SurfaceKHR> surface;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
};
std::unique_ptr<GlobalVk> g_vk;
std::unique_ptr<Core::System> g_system;
std::unique_ptr<EmuWindow_OHOS> g_window;
std::unique_ptr<Tegra::GPU> g_gpu;
std::unique_ptr<Service::AM::WindowSystem> g_window_system;
std::unique_ptr<Service::AM::EventObserver> g_event_observer;
void* g_surface = nullptr;
int g_surface_width = 0;
int g_surface_height = 0;

void SetStatus(const std::string& text) {
    g_status = text;
}

} // namespace

extern "C" int CitronOhosProbe() {
    LOG_INFO(Core, "[ohos] {}", CitronOhosProbeReserve());
    if (g_system) {
        return 0;
    }
    SetStatus("constructing System");
    g_system = std::make_unique<Core::System>();
    g_probe_result = 0;
    SetStatus("System constructed");
    return 0;
}

extern "C" void CitronOhosSetSurface(void* window, int width, int height) {
    g_surface = window;
    g_surface_width = width;
    g_surface_height = height;
    if (g_window) {
        g_window->SetWindow(window, u32(width), u32(height));
    }
}

extern "C" int CitronOhosInitialize() {
    if (!g_system) {
        const int rc = CitronOhosProbe();
        if (rc != 0) {
            return rc;
        }
    }
    if (!g_window) {
        g_window = std::make_unique<EmuWindow_OHOS>();
        if (g_surface) {
            g_window->SetWindow(g_surface, u32(g_surface_width), u32(g_surface_height));
        }
    }
    // 调试器一旦启用，KProcess::Run 会把主线程挂起等调试器（DebugSuspended=0x40），
    // 表现为应用线程 Runnable 但永远不执行。这里强制关闭。
    Settings::values.use_gdbstub = false;
    // NCE 在鸿蒙上仍会崩（ld-musl，FAR=0x8），暂用 Dynarmic 跑通；修好 NCE 后删掉这一行。
    Settings::values.cpu_backend = Settings::CpuBackend::Dynarmic;
    Common::Log::Initialize();
    LOG_INFO(Core, "[ohos] ======== 新的一次启动 ========");
    // 只把 Info 及以上写进 hilog；逐事件的调度/IPC 日志已降到 Trace，需要时再开。
    Common::Log::SetGlobalFilter(Common::Log::Filter{Common::Log::Level::Info});
    SetStatus("calling System::Initialize()");
    try {
        g_system->Initialize();
    } catch (const std::exception& e) {
        SetStatus(std::string("Initialize threw: ") + e.what());
        return 2;
    } catch (...) {
        SetStatus("Initialize threw unknown exception");
        return 3;
    }
    g_init_result = 0;
    SetStatus("System initialized");
    return 0;
}

extern "C" const char* CitronOhosStatus() {
    return g_status.c_str();
}

extern "C" int CitronOhosTestSurface() {
    if (!g_window) {
        SetStatus("TestSurface: no EmuWindow");
        return 10;
    }
    const auto& info = g_window->GetWindowInfo();
    if (!info.render_surface) {
        SetStatus("TestSurface: no native window");
        return 11;
    }
    if (!g_vk) {
        g_vk = std::make_unique<GlobalVk>();
    }
    if (!g_vk->library.IsOpen()) {
        SetStatus("TestSurface: libvulkan.so 打开失败");
        return 12;
    }
    try {
        if (!g_vk->instance) {
            g_vk->instance.emplace(Vulkan::CreateInstance(
                g_vk->library, g_vk->dld, VK_API_VERSION_1_1,
                Core::Frontend::WindowSystemType::Ohos, false));
        }
        if (!g_vk->surface) {
            g_vk->surface.emplace(Vulkan::CreateSurface(*g_vk->instance, info));
        }
        if (*(*g_vk->surface) == VK_NULL_HANDLE) {
            SetStatus("TestSurface: VkSurfaceKHR 为空");
            return 13;
        }
        SetStatus("TestSurface: VkInstance + VkSurfaceKHR 创建成功（常驻）");
        return 0;
    } catch (const std::exception& e) {
        SetStatus(std::string("TestSurface threw: ") + e.what());
        return 14;
    } catch (...) {
        SetStatus("TestSurface threw unknown exception");
        return 15;
    }
}

extern "C" int CitronOhosTestClear() {
    if (!g_window || !g_window->GetWindowInfo().render_surface) {
        SetStatus("TestClear: 没有原生窗口");
        return 20;
    }
    if (!g_vk || !g_vk->instance || !g_vk->surface) {
        SetStatus("TestClear: 需要先成功执行 TestSurface");
        return 21;
    }
    auto& instance = *g_vk->instance;
    auto& surface = *g_vk->surface;
    auto& dld = g_vk->dld;
    try {

        // 选支持 graphics + present 的物理设备
        const auto devices = instance.EnumeratePhysicalDevices();
        VkPhysicalDevice chosen = VK_NULL_HANDLE;
        uint32_t queue_family = 0;
        for (VkPhysicalDevice pd : devices) {
            uint32_t count = 0;
            dld.vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, nullptr);
            std::vector<VkQueueFamilyProperties> props(count);
            dld.vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, props.data());
            for (uint32_t i = 0; i < count; i++) {
                VkBool32 present = VK_FALSE;
                dld.vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, *surface, &present);
                if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    chosen = pd;
                    queue_family = i;
                    break;
                }
            }
            if (chosen != VK_NULL_HANDLE) {
                break;
            }
        }
        if (chosen == VK_NULL_HANDLE) {
            SetStatus("TestClear: 没有支持 present 的队列族");
            return 22;
        }
        VkPhysicalDeviceProperties props{};
        dld.vkGetPhysicalDeviceProperties(chosen, &props);
        SetStatus(std::string("TestClear: 选中 ") + props.deviceName);

        const Vulkan::vk::PhysicalDevice phys(chosen, dld);
        Vulkan::Device device(*instance, phys, *surface, dld);
        SetStatus(std::string("TestClear: Vulkan::Device 创建成功 (") + props.deviceName + ")");

        const auto& dd = device.GetDispatchLoader();
        VkDevice dev = *device.GetLogical();

        VkSurfaceCapabilitiesKHR caps{};
        dd.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(chosen, *surface, &caps);
        uint32_t format_count = 0;
        dd.vkGetPhysicalDeviceSurfaceFormatsKHR(chosen, *surface, &format_count, nullptr);
        if (format_count == 0) {
            SetStatus("TestClear: 无 surface format");
            return 23;
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        dd.vkGetPhysicalDeviceSurfaceFormatsKHR(chosen, *surface, &format_count, formats.data());

        VkExtent2D extent = caps.currentExtent;
        if (extent.width == UINT32_MAX) {
            extent = {1320, 918};
        }
        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
            image_count = caps.maxImageCount;
        }
        char diag[256];
        snprintf(diag, sizeof(diag),
                 "extent=%ux%u usage=0x%x compAlpha=0x%x fmt=%d cs=%d count=%u",
                 extent.width, extent.height, caps.supportedUsageFlags,
                 caps.supportedCompositeAlpha, (int)formats[0].format,
                 (int)formats[0].colorSpace, image_count);
        SetStatus(std::string("TestClear: caps ") + diag);

        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
            for (uint32_t bit = 1; bit <= VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR; bit <<= 1) {
                if (caps.supportedCompositeAlpha & bit) {
                    alpha = static_cast<VkCompositeAlphaFlagBitsKHR>(bit);
                    break;
                }
            }
        }
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
            usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        VkSwapchainCreateInfoKHR sci{};
        sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sci.surface = *surface;
        sci.minImageCount = image_count;
        sci.imageFormat = formats[0].format;
        sci.imageColorSpace = formats[0].colorSpace;
        sci.imageExtent = extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = usage;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = caps.currentTransform;
        sci.compositeAlpha = alpha;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkResult scr = dd.vkCreateSwapchainKHR(dev, &sci, nullptr, &swapchain);
        if (scr != VK_SUCCESS) {
            snprintf(diag, sizeof(diag), "TestClear: vkCreateSwapchainKHR rc=%d", (int)scr);
            SetStatus(diag);
            return 24;
        }
        uint32_t n = 0;
        dd.vkGetSwapchainImagesKHR(dev, swapchain, &n, nullptr);
        std::vector<VkImage> images(n);
        dd.vkGetSwapchainImagesKHR(dev, swapchain, &n, images.data());

        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = queue_family;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool pool = VK_NULL_HANDLE;
        dd.vkCreateCommandPool(dev, &pci, nullptr, &pool);
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        dd.vkAllocateCommandBuffers(dev, &cai, &cmd);

        VkSemaphoreCreateInfo semi{};
        semi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore acquire = VK_NULL_HANDLE;
        VkSemaphore render = VK_NULL_HANDLE;
        dd.vkCreateSemaphore(dev, &semi, nullptr, &acquire);
        dd.vkCreateSemaphore(dev, &semi, nullptr, &render);
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkFence fence = VK_NULL_HANDLE;
        dd.vkCreateFence(dev, &fci, nullptr, &fence);

        VkQueue queue = VK_NULL_HANDLE;
        dd.vkGetDeviceQueue(dev, queue_family, 0, &queue);

        uint32_t index = 0;
        VkResult r = dd.vkAcquireNextImageKHR(dev, swapchain, UINT64_MAX, acquire, VK_NULL_HANDLE,
                                              &index);
        if (r != VK_SUCCESS) {
            SetStatus("TestClear: acquire 失败");
            return 25;
        }
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        dd.vkBeginCommandBuffer(cmd, &bi);
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageMemoryBarrier to_dst{};
        to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.image = images[index];
        to_dst.subresourceRange = range;
        to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dd.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                &to_dst);
        VkClearColorValue color{};
        color.float32[0] = 0.1f;
        color.float32[1] = 0.8f;
        color.float32[2] = 0.2f;
        color.float32[3] = 1.0f;
        dd.vkCmdClearColorImage(cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                                &range);
        VkImageMemoryBarrier to_present = to_dst;
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_present.dstAccessMask = 0;
        dd.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                &to_present);
        dd.vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &acquire;
        si.pWaitDstStageMask = &stage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &render;
        dd.vkQueueSubmit(queue, 1, &si, fence);
        dd.vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &render;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &index;
        r = dd.vkQueuePresentKHR(queue, &pi);
        SetStatus(r == VK_SUCCESS ? "TestClear: 第一帧已提交 (绿色)" : "TestClear: present 失败");

        dd.vkDeviceWaitIdle(dev);
        // 清掉交换链，避免和后面 RendererVulkan 的交换链抢同一个窗口
        dd.vkDestroySwapchainKHR(dev, swapchain, nullptr);
        dd.vkDestroyCommandPool(dev, pool, nullptr);
        dd.vkDestroySemaphore(dev, acquire, nullptr);
        dd.vkDestroySemaphore(dev, render, nullptr);
        dd.vkDestroyFence(dev, fence, nullptr);
        if (r == VK_SUCCESS) {
            SetStatus("TestClear: 第一帧已提交并清理");
        }
        return r == VK_SUCCESS ? 0 : 26;
    } catch (const std::exception& e) {
        SetStatus(std::string("TestClear threw: ") + e.what());
        return 27;
    } catch (...) {
        SetStatus("TestClear threw unknown exception");
        return 28;
    }
}

extern "C" int CitronOhosCreateRenderer() {
    if (!g_system) {
        SetStatus("CreateRenderer: System 未初始化");
        return 30;
    }
    if (!g_window || !g_window->GetWindowInfo().render_surface) {
        SetStatus("CreateRenderer: 没有原生窗口");
        return 31;
    }
    SetStatus("CreateRenderer: 正在创建 Tegra::GPU + RendererVulkan");
    try {
        g_gpu = VideoCore::CreateGPU(*g_window, *g_system);
    } catch (const std::exception& e) {
        SetStatus(std::string("CreateRenderer threw: ") + e.what());
        return 32;
    } catch (...) {
        SetStatus("CreateRenderer threw unknown exception");
        return 33;
    }
    if (!g_gpu) {
        SetStatus("CreateRenderer: CreateGPU 返回空（渲染器初始化失败）");
        return 34;
    }
    SetStatus("CreateRenderer: RendererVulkan 创建成功");
    return 0;
}

extern "C" void CitronOhosSetPaths(const char* root) {
    namespace FS = Common::FS;
    const std::filesystem::path base(root && *root ? root : "/data/storage/el2/base/files");
    g_root = base.string();
    struct Entry {
        FS::CitronPath kind;
        const char* name;
    };
    const Entry entries[] = {
        {FS::CitronPath::CitronDir, ""},
        {FS::CitronPath::KeysDir, "keys"},
        {FS::CitronPath::NANDDir, "nand"},
        {FS::CitronPath::SDMCDir, "sdmc"},
        {FS::CitronPath::LogDir, "log"},
        {FS::CitronPath::CacheDir, "cache"},
        {FS::CitronPath::ConfigDir, "config"},
        {FS::CitronPath::ShaderDir, "shader"},
        {FS::CitronPath::DumpDir, "dump"},
        {FS::CitronPath::LoadDir, "load"},
    };
    for (const auto& e : entries) {
        const std::filesystem::path target = (*e.name == '\0') ? base : base / e.name;
        std::error_code ec;
        std::filesystem::create_directories(target, ec);
        FS::SetCitronPath(e.kind, target);
    }
    SetStatus("SetPaths: " + base.string());
}

extern "C" int CitronOhosLoad(const char* path, int program_index) {
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "Load 进入 tid=%{public}d", (int)gettid());
    if (!g_system) {
        SetStatus("Load: System 未初始化");
        return 40;
    }
    if (!g_window) {
        SetStatus("Load: 没有 EmuWindow");
        return 41;
    }
    if (!path || !*path) {
        SetStatus("Load: 路径为空");
        return 42;
    }
    InstallCrashHandlers();
    InstallDumpHandler();
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "Load: crash handler 安装完成");
    if (!g_root.empty()) {
        const std::string log_path = g_root + "/citron_stderr.log";
        if (FILE* f = std::fopen(log_path.c_str(), "w")) {
            std::setvbuf(f, nullptr, _IONBF, 0);
            dup2(fileno(f), 2);
            dup2(fileno(f), 1);
        }
    }
    // 探针阶段在同一个 OHNativeWindow 上建了常驻 VkSurface；游戏渲染器再建一套会抢不到窗口，
    // 帧进了 swapchain 但永远不上屏。加载前先把探针那套释放掉。
    if (g_vk) {
        if (g_vk->surface) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "Load: 释放探针 VkSurface");
            g_vk->surface.reset();
        }
        if (g_vk->instance) {
            g_vk->instance.reset();
        }
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "Load: 即将调用 System::Load");
    LOG_INFO(Core, "[ohos] 线程 {} 准备调用 System::Load", static_cast<int>(gettid()));
    Service::AM::FrontendAppletParameters params{};
    params.program_id = 0;
    params.applet_id = Service::AM::AppletId::Application;
    params.launch_type = Service::AM::LaunchType::FrontendInitiated;
    params.program_index = program_index;
    try {
        const auto result = g_system->Load(*g_window, path, params);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "Load: System::Load 返回 %{public}d",
                     static_cast<int>(result));
        SetStatus("Load: 返回码 " + std::to_string(static_cast<u32>(result)));
        return static_cast<int>(result);
    } catch (const std::exception& e) {
        SetStatus(std::string("Load threw: ") + e.what());
        return 43;
    } catch (...) {
        SetStatus("Load threw unknown exception");
        return 44;
    }
}

extern "C" int CitronOhosReloadKeys() {
    try {
        auto& km = Core::Crypto::KeyManager::Instance();
        km.ReloadKeys();
        int master_count = 0;
        for (u64 rev = 0; rev < 32; rev++) {
            if (km.HasKey(Core::Crypto::S128KeyType::Master, rev)) {
                ++master_count;
            }
        }
        SetStatus("ReloadKeys: master key=" + std::to_string(master_count) +
                  " AreKeysLoaded=" + (km.AreKeysLoaded() ? "true" : "false"));
        return master_count > 0 ? 0 : 60;
    } catch (const std::exception& e) {
        SetStatus(std::string("ReloadKeys threw: ") + e.what());
        return 61;
    } catch (...) {
        SetStatus("ReloadKeys threw unknown exception");
        return 62;
    }
}

extern "C" int CitronOhosLoadAsync(const char* path) {
    if (!path || !*path) {
        return 70;
    }
    std::string p{path};
    std::thread([p]() {
        OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "async Load 开始: %{public}s", p.c_str());
        const int rc = CitronOhosLoad(p.c_str(), 0);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "async Load 返回 rc=%{public}d status=%{public}s",
                     rc, CitronOhosStatus());
        if (rc == 0) {
            CitronOhosGpuStart();
            // 先恢复模拟（让系统模块跑起来注册服务），再启动 applet，
            // 否则 Applet 构造里请求 hid 服务会一直等下去。
            CitronOhosRun();
            // 合成帧测试暂不调用：它会让进程崩溃，挡住后续诊断
            CitronOhosDiagnose();
            std::thread([] { CitronOhosHeartbeat(); }).detach();
            // 不再由前端创建 WindowSystem/EventObserver：
            // guest 侧的 AM 系统模块（Service::AM::LoopProcess）自己会创建，
            // 前端再建一个会抢走待启动进程并让 AM 线程永远等在条件变量上。
        }
    }).detach();
    return 0;
}

extern "C" int CitronOhosRun() {
    if (!g_system) {
        SetStatus("Run: System 未初始化");
        return 80;
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "Run: 进入模拟主循环");
    try {
        g_system->Run();
        // 唤醒空闲的物理核：线程被置为 Runnable 后若没有中断，核会一直睡在 Idle()
        for (std::size_t core = 0; core < Core::Hardware::NUM_CPU_CORES; ++core) {
            g_system->Kernel().PhysicalCore(core).Interrupt();
        }
        OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON",
                     "Run: 主循环退出, timing_running=%{public}d paused=%{public}d",
                     static_cast<int>(g_system->CoreTiming().IsRunning()),
                     static_cast<int>(g_system->IsPaused()));
        SetStatus("Run: 主循环已退出");
        return 0;
    } catch (const std::exception& e) {
        SetStatus(std::string("Run threw: ") + e.what());
        return 81;
    } catch (...) {
        SetStatus("Run threw unknown exception");
        return 82;
    }
}

extern "C" const char* CitronOhosDiagnose() {
    if (!g_system) {
        SetStatus("Diagnose: System 未初始化");
        return CitronOhosStatus();
    }
    std::string out = "进程数=";
    auto processes = g_system->Kernel().GetProcessList();
    out += std::to_string(processes.size());
    for (auto& process : processes) {
        out += " | pid=" + std::to_string(process->GetProcessId());
        int thread_count = 0;
        for (auto& thread : process->GetThreadList()) {
            ++thread_count;
            const u32 raw_state = static_cast<u32>(thread.GetRawState());
            out += fmt::format(" tid={} st={} raw=0x{:x}", thread.GetThreadId(),
                               static_cast<int>(thread.GetState()), raw_state);
            // 调试挂起（0x40）会让线程永远不被调度；这里强制恢复
            if ((raw_state & 0x40) != 0) {
                thread.Resume(Kernel::SuspendType::Debug);
                out += "(已清除调试挂起)";
            }
        }
        out += " 线程=" + std::to_string(thread_count);
    }
    {
        int runnable = 0;
        int waiting = 0;
        int initialized = 0;
        int other = 0;
        std::map<u64, int> affinities;
        for (auto& process : processes) {
            for (auto& thread : process->GetThreadList()) {
                const int st = static_cast<int>(thread.GetRawState());
                if (st == 2) {
                    ++runnable;
                } else if (st == 1) {
                    ++waiting;
                } else if (st == 0) {
                    ++initialized;
                } else {
                    ++other;
                }
                s32 ideal = 0;
                u64 affinity = 0;
                thread.GetCoreMask(&ideal, &affinity);
                ++affinities[affinity];
            }
        }
        out += " || 状态: R=" + std::to_string(runnable) + " W=" + std::to_string(waiting) +
               " I=" + std::to_string(initialized) + " O=" + std::to_string(other) + " 亲和:";
        for (auto& [mask, count] : affinities) {
            out += fmt::format(" 0x{:x}x{}", mask, count);
        }
    }
    {
        auto& memory = g_system->ApplicationMemory();
        out += fmt::format(" || 代码:0x{:x},0x{:x},0x{:x}",
                           memory.Read32(0x80000000ULL), memory.Read32(0x80000004ULL),
                           memory.Read32(0x80000080ULL));
    }
    out += " || 队列: " + Kernel::KScheduler::DebugDumpPriorityQueue(g_system->Kernel());
    SetStatus(out);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "Diagnose: %{public}s", out.c_str());
    LOG_INFO(Core, "[ohos] {}", out);
    return CitronOhosStatus();
}

extern "C" int CitronOhosStartApplet() {
    if (!g_system) {
        SetStatus("StartApplet: System 未初始化");
        return 90;
    }
    try {
        if (!g_window_system) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "StartApplet: 构造 WindowSystem");
            g_window_system = std::make_unique<Service::AM::WindowSystem>(*g_system);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "StartApplet: 构造 EventObserver");
            g_event_observer =
                std::make_unique<Service::AM::EventObserver>(*g_system, *g_window_system);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "StartApplet: 调用 SetEventObserver");
            SetStatus("StartApplet: 正在启动 applet（会等待待启动进程）");
            g_window_system->SetEventObserver(g_event_observer.get());
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "StartApplet: SetEventObserver 返回");
            SetStatus("StartApplet: applet 已启动");
        }
        return 0;
    } catch (const std::exception& e) {
        SetStatus(std::string("StartApplet threw: ") + e.what());
        return 91;
    } catch (...) {
        SetStatus("StartApplet threw unknown exception");
        return 92;
    }
}

extern "C" int CitronOhosGpuStart() {
    if (!g_system) {
        SetStatus("GpuStart: System 未初始化");
        return 95;
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "GpuStart: 启动 GPU 线程");
    try {
        g_system->GPU().Start();
        // 所有 CPU 核都在这个屏障上等待 GPU 线程就绪；不解开则一行 guest 代码都不会执行
        g_system->GetCpuManager().OnGpuReady();
        SetStatus("GpuStart: GPU 线程已启动，CPU 屏障已放开");
        return 0;
    } catch (const std::exception& e) {
        SetStatus(std::string("GpuStart threw: ") + e.what());
        return 96;
    } catch (...) {
        SetStatus("GpuStart threw unknown exception");
        return 97;
    }
}

extern "C" void CitronOhosHeartbeat() {
    if (!g_system) {
        return;
    }
    for (int i = 0; i < 120; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::string out;
        for (std::size_t core = 0; core < Core::Hardware::NUM_CPU_CORES; ++core) {
            auto& scheduler = g_system->Kernel().Scheduler(core);
            auto* cur = scheduler.GetSchedulerCurrentThread();
            if (cur != nullptr) {
                const auto pc = cur->GetContext().pc;
                const auto lr = cur->GetContext().lr;
                out += fmt::format("c{}=cur{}pc={:x}lr={:x} ", core,
                                   static_cast<int>(cur->GetThreadId()), pc, lr);
            } else {
                out += fmt::format("c{}=-1 ", core);
            }
        }
        {
            auto& lock = g_system->Kernel().GlobalSchedulerContext().SchedulerLock();
            auto* owner = lock.DebugOwnerThread();
            out += fmt::format("锁: 持有者={} 层数={} ",
                               owner ? static_cast<int>(owner->GetThreadId()) : -1,
                               lock.DebugLockCount());
        }
        out += Kernel::KScheduler::DebugDumpPriorityQueue(g_system->Kernel());
        out += " " + Kernel::KScheduler::OhosDumpTrace();
        if (i == 3 && !g_root.empty()) {
            if (g_dump_fd < 0) {
                const std::string stack_path = g_root + "/citron_stacks.log";
                g_dump_fd = open(stack_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            }
            // 让每个 CPU 核线程把自己的调用栈打出来
            DIR* dir = opendir("/proc/self/task");
            if (dir != nullptr) {
                while (dirent* entry = readdir(dir)) {
                    const int tid = atoi(entry->d_name);
                    if (tid <= 0) {
                        continue;
                    }
                    char comm_path[64];
                    snprintf(comm_path, sizeof(comm_path), "/proc/self/task/%d/comm", tid);
                    FILE* f = fopen(comm_path, "r");
                    if (f == nullptr) {
                        continue;
                    }
                    char comm[32] = {};
                    if (fgets(comm, sizeof(comm), f) == nullptr) {
                        fclose(f);
                        continue;
                    }
                    fclose(f);
                    if (strncmp(comm, "CPUCore_0", 9) == 0) {
                        if (g_dump_fd >= 0) {
                            dprintf(g_dump_fd, ">>> 即将转储 %s tid=%d\n", comm, tid);
                        }
                        syscall(SYS_tgkill, getpid(), tid, SIGUSR2);
                    }
                }
                closedir(dir);
            }
        }
        LOG_INFO(Core, "[ohos] 心跳{} {}", i, out);
        // OHOS 基准：合成帧率与平均合成耗时
        {
            static u64 last_frames = 0;
            static u64 last_ns = 0;
            static auto last_time = std::chrono::steady_clock::now();
            const u64 frames = Tegra::g_ohos_composited_frames.load(std::memory_order_relaxed);
            const u64 total_ns = Tegra::g_ohos_composite_total_ns.load(std::memory_order_relaxed);
            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - last_time).count();
            if (seconds > 0.5) {
                const double fps = static_cast<double>(frames - last_frames) / seconds;
                const double avg_ms =
                    (frames > last_frames)
                        ? static_cast<double>(total_ns - last_ns) / (frames - last_frames) / 1e6
                        : 0.0;
                LOG_INFO(Core, "[ohos] 基准: fps={:.2f} 平均合成={:.2f} ms 累计帧={}", fps, avg_ms,
                         frames);
            }
            last_frames = frames;
            last_ns = total_ns;
            last_time = now;
        }
        CitronOhosDiagnose();
    }
}

extern "C" int CitronOhosCompositeTest() {
    if (!g_system) {
        SetStatus("CompositeTest: System 未初始化");
        return 100;
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "CompositeTest: 写入测试帧");
    constexpr u64 fb_addr = 0x88000000ULL;
    constexpr u32 width = 1280;
    constexpr u32 height = 720;
    constexpr u32 stride = width * 4;
    std::vector<u32> pixels(static_cast<std::size_t>(width) * height);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const bool odd = (((x / 64) + (y / 64)) & 1) != 0;
            pixels[static_cast<std::size_t>(y) * width + x] = odd ? 0xFFFF0000u : 0xFF0000FFu;
        }
    }
    auto& memory = g_system->Host1x().MemoryManager();
    memory.WriteBlock(fb_addr, pixels.data(), pixels.size() * sizeof(u32));
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "CompositeTest: 显存写入完成");
    {
        u32 probe = 0;
        memory.ReadBlock(fb_addr, &probe, sizeof(probe));
        OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "CompositeTest: 读回=0x%{public}x", probe);
    }

    Tegra::FramebufferConfig config{};
    config.address = fb_addr;
    config.offset = 0;
    config.width = width;
    config.height = height;
    config.stride = stride;
    config.pixel_format = Service::android::PixelFormat::Rgba8888;
    config.crop_rect = Common::Rectangle<int>{static_cast<int>(width), static_cast<int>(height)};
    config.blending = Tegra::BlendMode::Opaque;

    try {
        // 走 nvdisp 同样的路径：入队 → TickGPU → 等同步操作完成
        std::vector<Tegra::FramebufferConfig> layers{config};
        g_system->GPU().RequestComposite(std::move(layers), {});
    } catch (const std::exception& e) {
        SetStatus(std::string("CompositeTest 抛异常: ") + e.what());
        return 101;
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD006, "VKCITRON", "CompositeTest: 已提交 Composite");
    return 0;
}

extern "C" const char* CitronOhosProbeReserve() {
    // 试验：分段预留 256 GiB 虚拟地址空间（每段 4 GiB）
    constexpr size_t total = 1ULL << 38;
    constexpr size_t chunk = 1ULL << 32;
    void* base = mmap(nullptr, chunk, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (base == MAP_FAILED) {
        SetStatus("ProbeReserve: 首段预留失败");
        return CitronOhosStatus();
    }
    size_t reserved = chunk;
    for (; reserved < total; reserved += chunk) {
        void* want = static_cast<char*>(base) + reserved;
        void* got = mmap(want, chunk, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        if (got == MAP_FAILED) {
            const auto reached = reserved >> 30;
            munmap(base, reserved);
            SetStatus("ProbeReserve: 预留到 " + std::to_string(reached) + " GiB 失败（已释放）");
            return CitronOhosStatus();
        }
    }
    munmap(base, reserved);
    SetStatus("ProbeReserve: 分段预留 256 GiB 成功（已释放）");
    return CitronOhosStatus();
}
