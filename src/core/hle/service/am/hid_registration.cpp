// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/am/hid_registration.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/os/process.h"
#include "core/hle/service/sm/sm.h"
#include "hid_core/resource_manager.h"

namespace Service::AM {

HidRegistration::HidRegistration(Core::System& system, Process& process) : m_process(process) {
    LOG_INFO(Service_AM, "[ohos] HidRegistration: 请求 hid 服务");
    // 服务线程现在能正常跑起来，恢复阻塞获取：必须完成 ARUID 注册，
    // 否则 guest 侧申请 applet resource 共享内存会返回 0x82ECA
    m_hid_server = system.ServiceManager().GetService<HID::IHidServer>("hid", true);
    LOG_INFO(Service_AM, "[ohos] HidRegistration: hid 服务={}",
             m_hid_server ? "已就绪" : "未就绪（跳过注册）");

    if (m_hid_server && m_process.IsInitialized()) {
        m_hid_server->GetResourceManager()->RegisterAppletResourceUserId(m_process.GetProcessId(),
                                                                         true);
        m_hid_server->GetResourceManager()->SetAruidValidForVibration(m_process.GetProcessId(),
                                                                      true);
    }
}

HidRegistration::~HidRegistration() {
    if (m_process.IsInitialized()) {
        m_hid_server->GetResourceManager()->SetAruidValidForVibration(m_process.GetProcessId(),
                                                                      false);
        m_hid_server->GetResourceManager()->UnregisterAppletResourceUserId(
            m_process.GetProcessId());
    }
}

void HidRegistration::EnableAppletToGetInput(bool enable) {
    if (m_hid_server && m_process.IsInitialized()) {
        m_hid_server->GetResourceManager()->SetAruidValidForVibration(m_process.GetProcessId(),
                                                                      enable);
        m_hid_server->GetResourceManager()->EnableInput(m_process.GetProcessId(), enable);
    }
}

} // namespace Service::AM
