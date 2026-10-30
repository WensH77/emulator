#!/bin/bash
# 构建 citron-ohos 共享库
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony
CMAKE=$SDK/native/build-tools/cmake/bin/cmake

source "$ROOT/ohos/branch-guard.sh" "$ROOT"
echo "分支: $CITRON_BRANCH ($CITRON_COMMIT)"

if [ ! -d "$ROOT/build-ohos" ]; then
    "$ROOT/ohos/configure.sh" "$@"
fi

$CMAKE --build "$ROOT/build-ohos" --target citron-ohos -j8

INFO="$ROOT/build-ohos/ohos-build-info.txt"
{
    echo "branch=$CITRON_BRANCH"
    echo "commit=$CITRON_COMMIT"
    echo "date=$(date -u +%FT%TZ)"
    # 依赖目录以 build-ohos 缓存里实际使用的路径为准，避免登记信息失真
    CACHE="$ROOT/build-ohos/CMakeCache.txt"
    deps=""
    if [ -f "$CACHE" ]; then
        openssl_root="$(grep -m1 '^OPENSSL_ROOT_DIR:' "$CACHE" | cut -d= -f2-)"
        [ -n "$openssl_root" ] && deps="$(dirname "$openssl_root")"
    fi
    echo "deps=${deps:-unknown}"
} > "$INFO"
echo "产物: $ROOT/build-ohos/src/ohos/libcitron_ohos.so"
cat "$INFO"
