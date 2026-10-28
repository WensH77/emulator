#!/bin/bash
# 交叉编译 Citron 在 OHOS 上所需的第三方依赖
# 用法: ./ohos/build-deps.sh <工作目录>
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-${CITRON_OHOS_DEPS:-/tmp/citron-ohos-deps}}"
SDK=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony
CLANG=$SDK/native/llvm/bin/clang
CLANGXX=$SDK/native/llvm/bin/clang++
AR=$SDK/native/llvm/bin/llvm-ar
RANLIB=$SDK/native/llvm/bin/llvm-ranlib
SYSROOT=$SDK/native/sysroot

mkdir -p "$WORK"
cd "$WORK"

# ── OpenSSL ────────────────────────────────────────────────────────────────
if [ ! -d "$WORK/openssl-ohos" ]; then
    if [ ! -d "$WORK/openssl-3.4.1" ]; then
        curl -sL -o openssl.tar.gz https://github.com/openssl/openssl/releases/download/openssl-3.4.1/openssl-3.4.1.tar.gz
        tar xzf openssl.tar.gz
    fi
    cd openssl-3.4.1
    export CC=$CLANG AR=$AR RANLIB=$RANLIB
    export CFLAGS="--target=aarch64-linux-ohos --sysroot=$SYSROOT -fPIC"
    export CXXFLAGS="$CFLAGS"
    export LDFLAGS="--target=aarch64-linux-ohos --sysroot=$SYSROOT"
    ./Configure linux-aarch64 --prefix="$WORK/openssl-ohos" \
        --openssldir="$WORK/openssl-ohos/ssl" no-shared no-tests no-asm
    make -j8
    make install_sw
    cd "$WORK"
fi

# ── FFmpeg ─────────────────────────────────────────────────────────────────
if [ ! -d "$WORK/ffmpeg-ohos" ]; then
    FFMPEG_SRC=$(ls -d "$ROOT"/build-ohos/_cpm_cache/ffmpeg_src/*/ 2>/dev/null | head -1 || true)
    if [ -z "$FFMPEG_SRC" ]; then
        echo "需要先跑一次 CMake configure 以获取 ffmpeg 源码" >&2
        exit 1
    fi
    cd "$FFMPEG_SRC"
    cat > "$WORK/cc-ohos" <<WRAP
#!/bin/sh
exec $CLANG --target=aarch64-linux-ohos --sysroot=$SYSROOT "\$@"
WRAP
    cat > "$WORK/cxx-ohos" <<WRAP
#!/bin/sh
exec $CLANGXX --target=aarch64-linux-ohos --sysroot=$SYSROOT "\$@"
WRAP
    chmod +x "$WORK/cc-ohos" "$WORK/cxx-ohos"
    ./configure --prefix="$WORK/ffmpeg-ohos" \
        --enable-cross-compile --target-os=linux --arch=aarch64 \
        --cc="$WORK/cc-ohos" --cxx="$WORK/cxx-ohos" --ar=$AR --ranlib=$RANLIB \
        --enable-static --disable-shared --enable-pic \
        --disable-programs --disable-doc --disable-network --disable-avdevice --disable-vulkan
    make -j8
    make install
fi

echo "依赖就绪: $WORK/openssl-ohos  $WORK/ffmpeg-ohos"
