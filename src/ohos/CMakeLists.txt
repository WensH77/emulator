# SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(citron-ohos SHARED
    native.cpp
    native.h
    emu_window/emu_window.cpp
    emu_window/emu_window.h
)

target_link_libraries(citron-ohos PRIVATE
    audio_core
    common
    core
    video_core
    frontend_common
    input_common
    Vulkan::Headers
    libhilog_ndk.z.so
)

if (TARGET dynarmic)
    target_link_libraries(citron-ohos PRIVATE dynarmic)
endif()

target_compile_definitions(citron-ohos PRIVATE CITRON_OHOS=1)
target_include_directories(citron-ohos PRIVATE ${CMAKE_SOURCE_DIR}/src)

set_target_properties(citron-ohos PROPERTIES
    OUTPUT_NAME "citron_ohos"
    PREFIX "lib"
)
