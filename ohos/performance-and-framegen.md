# 性能基准与补帧方案（HarmonyOS 移植）

## 一、当前基准数据（2026-10-28 实测）

测试对象：`nx-hbmenu`（GUI homebrew，2.4 MB），在 Mate 80 RS 上跑在自己的错误提示界面。

日志里每 2 秒一行：

```
[ohos] 基准: fps=59.96 平均合成=6.17 ms 累计帧=1592
```

读法：

| 字段 | 来源 | 含义 |
|---|---|---|
| `fps` | `g_ohos_composited_frames` 的增量 / 经过时间 | guest 每秒提交并被合成的帧数 |
| `平均合成` | `g_ohos_composite_total_ns` / 帧数 | 单帧从 `RendererVulkan::Composite` 入口到返回的耗时 |
| `累计帧` | 计数器总值 | 自进程启动以来的合成帧数 |

两个计数器定义在 `src/video_core/gpu.h`（`Tegra::g_ohos_composited_frames`、
`Tegra::g_ohos_composite_total_ns`），在 `src/video_core/gpu.cpp` 的
`RequestComposite` 里两条路径（无 fence 直接合成、有 fence 等同步后合成）都会累加。
日志由 `src/ohos/native.cpp` 的心跳线程输出。

当前 60 fps 是**显示刷新率上限**（`VK_PRESENT_MODE_FIFO_KHR`），不是性能瓶颈；
6.17 ms 的合成耗时说明单帧还有余量。

## 二、基准还能怎么扩展

1. **拆解合成耗时**：把 `Composite` 内部拆成 blit、present 两段分别计时，看瓶颈在哪。
2. **CPU 侧负担**：心跳里已经能读内核线程队列，可再加「每核 guest 执行占比」。
3. **不同 NRO 对比**：hbmenu（文字界面，负载低）→ appstore（列表+图片）→ 之后的真实游戏，
   同一套计数器直接对比。
4. **压力场景**：换成 720p/1080p 全屏刷新的 homebrew，观察 `平均合成` 是否逼近 16.7 ms。

## 三、补帧（frame generation）方案

### 为什么需要

Switch 上不少游戏按 30 fps 出帧。HarmonyOS 这块屏幕是 120 Hz，如果直接把 30 fps 的帧送去
present，观感就是 30 fps。补帧的目标是把它插值成 60/120 fps 的显示节奏。

### 可选方案（按成本从低到高）

| 方案 | 做法 | 成本 | 画质影响 |
|---|---|---|---|
| A. 帧重复 + 节奏控制 | 两次 guest 帧之间把上一帧再 present 一次 | 极低（一次 present） | 无（只是更均匀的显示节奏） |
| B. 相邻帧线性混合 | 用 alpha 在上一帧与当前帧之间插值 | 低（一次全屏 blit + 混合） | 有拖影，快速运动明显 |
| C. 运动补偿插值（MEMC） | 估计光流后按运动矢量重采样 | 高（光流 + 重采样，移动端难实时） | 最好，但实现复杂、可能出错块 |

建议先做 A，验证「多 present 一次」在鸿蒙上的可行性与开销；有数据后再评估 B。

### A 方案的落点

1. **保留上一帧输出**：`blit_swapchain` 每帧把 guest framebuffer 画到交换链图像上。
   在 `RendererVulkan::Composite` 返回后，额外保存这次输出的交换链图像索引。
2. **插一次 present**：在 `Tegra::GPU::Impl::RequestComposite` 里，当检测到「距上一帧的
   时间超过 1/60 秒」时，在合成后用 `PresentManager` 再 present 一次同一张图像。
   注意交换链图像被重复 present 需要遵守 present 语义（同一张图可直接再 present，
   内容不变）。
3. **节奏对齐**：用 host 的 `steady_clock` 计算两次 guest 帧的间隔，决定插 0 次还是 1 次；
   不要盲目每次插两帧，否则会把 30 fps 变成 90 fps 造成抖动。

### B 方案的落点（如果 A 有效）

1. 保留上一帧的**输出纹理**（不是交换链图像，用一张专用 intermediate image）。
2. `Composite` 时：先把当前 guest 帧 blit 到 intermediate，再用一个全屏 quad 把
   `previous * (1-t) + current * t` 画到交换链图像上。
3. `t` 由两次 guest 帧的时间比例计算，范围 [0,1)。
4. 需要新增一条 present 专用管线（Citron 已有 `present/layer.cpp` 的 blit 管线可复用）。

### 风险与必须记录的点

- **输入延迟**：任何插值都会让显示落后于输入至少一帧，格斗/音游类不能开。
- **分辨率/格式**：Mobile Maleoon 对某些格式不支持（`Format=78/71/17 ...` 的 warning），
  插值管线的格式必须与交换链一致。
- **音频同步**：补帧只改变显示节奏，不能改 emulated 时间，否则音频会漂。
- **与 VSync 的交互**：目前用 `VK_PRESENT_MODE_FIFO_KHR`，多 present 会被 vsync 节流，
  需要先确认「同一帧 present 两次」在这块屏上是否真的产生两次刷新。

## 四、下一步实验顺序

1. 确认 A 方案可行：手动在 `Composite` 后立即再 present 一次，观察 fps 计数器与实际观感
2. 若可行，加入「间隔判断」逻辑，只在 guest 帧率低于显示刷新时补
3. 有稳定数据后再评估 B（线性混合），并单独开一条性能基准对比
