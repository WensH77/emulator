#!/bin/bash
# 构建 citron-ohos 共享库
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony
CMAKE=$SDK/native/build-tools/cmake/bin/cmake

if [ ! -d "$ROOT/build-ohos" ]; then
    "$ROOT/ohos/configure.sh" "$@"
fi

$CMAKE --build "$ROOT/build-ohos" --target citron-ohos -j8
echo "产物: $ROOT/build-ohos/src/ohos/libcitron_ohos.so"
