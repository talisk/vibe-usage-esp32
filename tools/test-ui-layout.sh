#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ ! -f "${repo_root}/firmware/ai-passport/managed_components/lvgl__lvgl/CMakeLists.txt" ]]; then
    echo "ERROR: build Passport once to resolve the pinned LVGL component." >&2
    exit 1
fi
if [[ ! -f "${repo_root}/firmware/zectrix/managed_components/espressif__qrcode/qrcodegen.c" ]]; then
    echo "ERROR: build Note once to resolve the pinned QR component." >&2
    exit 1
fi
ui_build_dir="${VIBE_UI_BUILD_DIR:-${repo_root}/build/ui-layout}"
cmake -S "${repo_root}/tests/host/lvgl" -B "${ui_build_dir}" \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build "${ui_build_dir}" --parallel 6
"${ui_build_dir}/test_passport_layout"
"${ui_build_dir}/test_note_qr"
