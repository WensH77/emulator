# Citron → HarmonyOS 移植笔记

## 目录内容

| 文件 | 作用 |
|---|---|
| `toolchain-ohos.cmake` | CMake 工具链（OHOS clang 15 + sysroot） |
| `clang-asm-ohos` | 汇编编译器包装，保证 `.s` 文件带 `--target/--sysroot` |
| `pkg-config-stub.sh` | 假 pkg-config：对可选依赖一律返回 not found |
| `build-deps.sh` | 交叉编译 OpenSSL 3.4.1 与 FFmpeg 静态库 |
| `configure.sh` | 为 OHOS 配置 Citron |
| `build.sh` | 构建 `libcitron_ohos.so` |
| `../src/ohos/` | 导出给 HAP 的 C 接口（`CitronOhosProbe/Initialize/Status`） |

## 构建步骤

```bash
./ohos/build-deps.sh /tmp/citron-ohos-deps   # 首次：编 OpenSSL + FFmpeg
./ohos/configure.sh  /tmp/citron-ohos-deps   # 首次：配置
./ohos/build.sh                              # 出 libcitron_ohos.so
```

## 已做的源码移植改动

1. `src/common/ohos_ranges_compat.h`（新增）：补齐 libc++ 15 缺失的 ranges 算法、
   `make_unique_for_overwrite`、pthread 取消接口；用 `-include` 全局注入。
2. `src/common/fs/fs_util.cpp`：`ranges::find` → `std::find`。
3. `src/common/host_memory.cpp`：去掉不可达 `throw`、`ranges::copy`、结构化绑定捕获。
4. `src/common/thread.cpp`：`__OHOS__` 走 `sched_setaffinity`。
5. `src/core/frontend/emu_window.h`：新增 `WindowSystemType::Ohos`。
6. `src/core/device_memory.cpp`：虚拟预留从 256 GiB 降到 128 GiB（鸿蒙地址空间上限）。
7. `src/shader_recompiler/backend/spirv/emit_spirv_warp.cpp`：去掉当前 sirit 不提供的
   `OpGroupNonUniformQuadSwap`，走通用 butterfly 路径。
8. `src/video_core/vulkan_common/vulkan.h`：OHOS 不定义 X11/Wayland 平台宏。
9. `src/video_core/vulkan_common/vulkan_{instance,surface}.cpp`：加 OHOS 分支。
10. `externals/dynarmic/.../{arm.h,a64.h}`：聚合初始化改为显式 `Handler{...}`
    （clang 15 不支持 P0960）。

## 当前状态

- `libcore.a` / `libcitron_ohos.so` 均可交叉编译
- `Core::System` 构造成功
- `System::Initialize()` 曾因 256 GiB 虚拟预留失败抛 `std::bad_alloc`，已改为 128 GiB，待在真机复测

## 真机验证记录（Mate 80 RS / HarmonyOS 7 / API 26）

| 项目 | 结果 |
|---|---|
| `System` 构造 | ✅ |
| `System::Initialize()` | ✅（修复：虚拟预留 256 GiB → 128 GiB） |
| Dynarmic JIT（RW→RX 执行 ARM64） | ✅ |
| SIGSEGV 捕获并恢复（NCE 依赖） | ✅ |
| `vkCreateSurfaceOHOS` | ✅ |
| `Vulkan::Device`（Citron 自己的特性检查） | ✅ |
| 交换链 + 清屏出帧 | ✅ |
| `VideoCore::CreateGPU` / `RendererVulkan` | ✅ |
| `KeyManager::ReloadKeys()` 解析 prod.keys | ✅（8 个 master key） |
| `System::Load(NSP)` | ⬜ 需要真实游戏文件 |

### 真机上必须遵守的两条规则

1. **一个 OHNativeWindow 只能有一个活跃交换链**。独立渲染器和 Citron 不能同时持有窗口；
   任何测试用完交换链必须销毁，否则后面的 `vkCreateSwapchainKHR` 返回
   `VK_ERROR_SURFACE_LOST_KHR (-1000000000)`。
2. **进程虚拟地址空间上限在 128 GiB 与 256 GiB 之间**。`DeviceMemory` 的
   `VirtualReserveSize` 已改成 `1ULL << 37`。

### 用真实游戏测试的步骤

1. 手机打开应用，点「导入密钥」，选自己的 `prod.keys`（会复制到 `files/keys/prod.keys`）
2. 点「导入游戏并启动」，选自己的 `.nsp`
3. 状态栏会依次显示 SetPaths / ReloadKeys / Load 的返回码

预期仍可能失败的点：固件缺失（`ErrorSystemFiles`）、共享字体缺失、游戏版本不支持。

### 已知问题（2026-10-28 实测）

- **损坏/伪造的 NSP 会让进程 SIGABRT**：用一个 1 MB 随机数据文件调用 `System::Load`，
  进程被 signal 6 终止（Citron 加载器内部断言）。真实游戏文件不会触发，但如果要做
  用户导入功能，建议先校验 NSP/XCI 魔数再调用 Load。
- **每次 `System::Load` 失败后不要重复调用**：加载器内部状态未清理，重复调用行为未验证。

---

## 2026-10-28 晚：homebrew 出画面（本阶段结论）

### 结果

用开源 homebrew `nx-hbmenu`（2.4 MB，GUI 程序，不需要任何密钥）在 Mate 80 RS 上跑到
**画面出现在鸿蒙屏幕上**：

- guest 侧完成 libnx 初始化，进程持续运行（CPU 时间持续增长）
- guest 提交帧 → `GPU::Impl::RequestComposite` → `RendererVulkan::Composite` → 鸿蒙交换链
- 屏幕显示的是 hbmenu 自己渲染的界面（它报 `launchInit() failed`，是程序自身缺少
  SD 卡内容，不是移植故障）

### 本阶段新修的移植问题（按发现顺序）

| 问题 | 现象 | 修法 |
|---|---|---|
| 缺 `GPU().Start()` + `CpuManager::OnGpuReady()` | 4 个 CPU 核全部阻塞在屏障，guest 一行代码都不执行 | 前端在 Load 后依次调用 |
| `KScheduler::Activate()` 不强制调度 | 主线程 fiber 直接落进 `std::unreachable()` | `Activate` 里强制置 `needs_scheduling` |
| 前端创建 WindowSystem/EventObserver | 抢走待启动进程，guest 的 AM 系统模块永远等条件变量 | 前端不再创建，交给 `Service::AM::LoopProcess` |
| `HidRegistration` 非阻塞 | ARUID 未注册，guest 取 applet resource 共享内存返回 `0x82ECA` → panic | 服务线程能跑之后改回阻塞获取 |
| NRO 按普通进程创建 | 异常处理走错分支（`hbl=false`） | `LoadFromMetadata(..., true)` |
| 调试挂起位 `0x40` | 线程 Runnable 但不在任何队列，永远不执行 | 检测后 `Resume(SuspendType::Debug)` |
| guest 虚拟地址空间超出 arena | libnx 随机选到 245/368 GiB，`HostMemory::Map` 断言失败 | 页表初始化时把 alias/heap/stack/kernel-map 裁进 160 GiB |
| `ChannelSetupCaches::channel_state` 未初始化 | 首次合成必崩（野指针非 null，判空失效） | `P* channel_state{};` |

### 三个必须知道的平台事实

1. **单窗口单交换链**：任何测试用完交换链必须销毁。
2. **虚拟地址空间很小**：单次 128 GiB 可以，256 GiB 单次 mmap 直接 OOM；分段 4 GiB × N
   到 60 GiB 左右也失败。所以 arena 固定 160 GiB，guest 区域必须裁剪。
3. **Maleoon 936 缺若干 Vulkan 特性**：`dualSrcBlend`、`logicOp`、`multiViewport`、
   `vertexPipelineStoresAndAtomics` 不支持，`maxViewports=1`；Citron 会打印 warning
   并以移动端降级继续，`VK_EXT_extended_dynamic_state` 被自动禁用。

### 待办

- 真实游戏（NSP/XCI）仍需要用户自己主机的 `prod.keys`，没有合法来源
- **测试用的 `.nro` 已全部移除**（HAP 资源与手机沙箱都不再保留）。要复现「出画面」
  那条链路，需要临时把 hbmenu.nro 放回 `entry/src/main/resources/rawfile/` 并恢复
  自动加载逻辑；正常使用时应用只做初始化 + 手动导入游戏。
- 性能基准与补帧方案见 `ohos/performance-and-framegen.md`
