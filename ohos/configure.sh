#!/bin/bash
# 为 OHOS 配置 Citron
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-${CITRON_OHOS_DEPS:-/tmp/citron-ohos-deps}}"
SDK=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony
CMAKE=$SDK/native/build-tools/cmake/bin/cmake
COMMON="--target=aarch64-linux-ohos --sysroot=$SDK/native/sysroot -fPIC -DARCHITECTURE_arm64=1 -ffile-prefix-map=$ROOT=."

export PATH=/opt/homebrew/bin:$PATH
echo "依赖目录: $WORK"
export FFMPEG_DIR="$WORK/ffmpeg-ohos"

$CMAKE -S "$ROOT" -B "$ROOT/build-ohos" \
    -U "FFmpeg_*" -U "FFMPEG_*" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/ohos/toolchain-ohos.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_ASM_COMPILER="$ROOT/ohos/clang-asm-ohos" \
    -DCMAKE_C_FLAGS="$COMMON" \
    -DCMAKE_CXX_FLAGS="$COMMON -include $ROOT/src/common/ohos_ranges_compat.h" \
    -DARCHITECTURE=arm64 -DARCHITECTURE_arm64=1 -DCITRON_USE_CPM=ON \
    -DENABLE_QT=OFF -DENABLE_SDL2=OFF -DENABLE_TESTS=OFF -DENABLE_CUBEB=OFF \
    -DENABLE_WEB_SERVICE=OFF -DENABLE_OPENSSL=OFF -DENABLE_VULKAN=ON -DENABLE_LIBUSB=OFF \
    -DOPENSSL_ROOT_DIR="$WORK/openssl-ohos" -DOPENSSL_USE_STATIC_LIBS=TRUE \
    -DPKG_CONFIG_EXECUTABLE="$ROOT/ohos/pkg-config-stub.sh" \
    -DCITRON_USE_BUNDLED_FFMPEG=OFF -DCITRON_DOWNLOAD_TIME_ZONE_DATA=ON \
    -DCITRON_OHOS=ON
