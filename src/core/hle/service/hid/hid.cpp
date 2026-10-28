// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/hid/hid_debug_server.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/hid/hid_system_server.h"
#include "core/hle/service/hid/hidbus.h"
#include "core/hle/service/hid/irs.h"
#include "core/hle/service/hid/xcd.h"
#include "core/hle/service/server_manager.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/hid_firmware_settings.h"

namespace Service::HID {

void LoopProcess(Core::System& system) {
    LOG_INFO(Service_HID, "[ohos] HID::LoopProcess 开始执行");
    auto server_manager = std::make_unique<ServerManager>(system);
    std::shared_ptr<HidFirmwareSettings> firmware_settings =
        std::make_shared<HidFirmwareSettings>(system);
    std::shared_ptr<ResourceManager> resource_manager =
        std::make_shared<ResourceManager>(system, firmware_settings);

    LOG_INFO(Service_HID, "[ohos] HID: 初始化 ResourceManager");
    resource_manager->Initialize();
    LOG_INFO(Service_HID, "[ohos] HID: ResourceManager 初始化完成");

    server_manager->RegisterNamedService(
        "hid", std::make_shared<IHidServer>(system, resource_manager, firmware_settings));
    server_manager->RegisterNamedService(
        "hid:dbg", std::make_shared<IHidDebugServer>(system, resource_manager, firmware_settings));
    server_manager->RegisterNamedService(
        "hid:sys", std::make_shared<IHidSystemServer>(system, resource_manager, firmware_settings));

    server_manager->RegisterNamedService("hidbus", std::make_shared<Hidbus>(system));

    server_manager->RegisterNamedService("irs", std::make_shared<IRS::IRS>(system));
    server_manager->RegisterNamedService("irs:sys", std::make_shared<IRS::IRS_SYS>(system));

    server_manager->RegisterNamedService("xcd:sys", std::make_shared<XCD_SYS>(system));

    system.RunServer(std::move(server_manager));
}

} // namespace Service::HID
