#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
board="${1:-all}"
if [[ $# -gt 1 || ! "${board}" =~ ^(all|passport|note4)$ ]]; then
    echo "Usage: $0 [all|passport|note4]" >&2
    exit 2
fi
if [[ "${board}" != "note4" && ! -f "${repo_root}/firmware/ai-passport/managed_components/lvgl__lvgl/CMakeLists.txt" ]]; then
    echo "ERROR: build Passport once to resolve the pinned LVGL component." >&2
    exit 1
fi
if [[ "${board}" != "passport" && ! -f "${repo_root}/firmware/zectrix/managed_components/espressif__qrcode/qrcodegen.c" ]]; then
    echo "ERROR: build Note once to resolve the pinned QR component." >&2
    exit 1
fi
ui_build_dir="${VIBE_UI_BUILD_DIR:-${repo_root}/build/ui-layout}"
cmake -S "${repo_root}/tests/host/lvgl" -B "${ui_build_dir}" \
    -DCMAKE_BUILD_TYPE=Debug -DVIBE_UI_TARGET="${board}"
cmake --build "${ui_build_dir}" --parallel 6
if [[ "${board}" != "note4" ]]; then
    "${ui_build_dir}/test_passport_layout"
fi
if [[ "${board}" != "passport" ]]; then
    "${ui_build_dir}/test_note_qr"
fi
