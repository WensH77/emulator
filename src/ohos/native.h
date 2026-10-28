// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 最小可行性探针：构造 Core::System 并返回状态码
// 0 = 成功
int CitronOhosProbe();

// 初始化内核（headless），返回 0 表示成功
int CitronOhosInitialize();

// 设置原生窗口（OHNativeWindow*），必须在 CitronOhosInitialize 之前调用
void CitronOhosSetSurface(void* window, int width, int height);

// 用 Citron 自己的 Vulkan 封装在 OHOS 原生窗口上创建 instance + surface
int CitronOhosTestSurface();

// 用 Citron 的 Device 建交换链并清屏一帧
int CitronOhosTestClear();

// 创建 Tegra::GPU + RendererVulkan（Citron 真正的渲染器）
int CitronOhosCreateRenderer();

// 设置 Citron 的数据目录（应用沙箱）
void CitronOhosSetPaths(const char* root);

// 加载游戏文件（NSP/XCI），返回 SystemResultStatus
int CitronOhosLoad(const char* path, int program_index);

// 从 KeysDir 重新加载 prod.keys / title.keys
int CitronOhosReloadKeys();

// 在后台线程加载（避免阻塞 UI 被系统判为无响应）
int CitronOhosLoadAsync(const char* path);

// 进入模拟主循环（必须在 Load 成功后调用）
int CitronOhosRun();

// 输出内核进程/线程状态
const char* CitronOhosDiagnose();

// 创建 WindowSystem / EventObserver，真正启动 applet 与主线程
int CitronOhosStartApplet();

// 启动 GPU 线程（CPU 核与 GPU 之间有屏障，不启动 CPU 会一直阻塞）
int CitronOhosGpuStart();

// 每 2 秒打印一次调度状态（诊断用）
void CitronOhosHeartbeat();

// 用测试图案驱动 Citron 渲染器提交一帧
int CitronOhosCompositeTest();

// 试探分段预留 256 GiB 是否可行
const char* CitronOhosProbeReserve();

// 返回一个静态字符串描述当前状态，调用方不需要释放
const char* CitronOhosStatus();

#ifdef __cplusplus
}
#endif
