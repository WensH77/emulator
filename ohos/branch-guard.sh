#!/bin/bash
# 分支守卫：鸿蒙移植只在 harmony 分支维护，构建前强制检查。
# 被 configure.sh / build.sh 以 source 方式引入。
# 确需在其它分支构建时：CITRON_ALLOW_ANY_BRANCH=1
set -euo pipefail

CITRON_GUARD_ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
CITRON_BRANCH="$(git -C "$CITRON_GUARD_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"

if [ "$CITRON_BRANCH" != "harmony" ] && [ "${CITRON_ALLOW_ANY_BRANCH:-0}" != "1" ]; then
    echo "错误: 当前分支是 '$CITRON_BRANCH'，鸿蒙移植只在 harmony 分支上维护。" >&2
    echo "      git -C $CITRON_GUARD_ROOT checkout harmony" >&2
    echo "      （确实需要在其它分支构建时: CITRON_ALLOW_ANY_BRANCH=1）" >&2
    exit 1
fi

CITRON_COMMIT="$(git -C "$CITRON_GUARD_ROOT" rev-parse --short=10 HEAD 2>/dev/null || echo unknown)"
export CITRON_BRANCH CITRON_COMMIT
