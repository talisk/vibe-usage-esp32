#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d /tmp/vibe-usage-host-tests.XXXXXX)"
trap 'case "${test_dir}" in /tmp/vibe-usage-host-tests.*) rm -rf -- "${test_dir}" ;; esac' EXIT

cc="${CC:-cc}"
flags=(-std=c11 -Wall -Wextra -Wpedantic -Werror)
if [[ "${VIBE_SANITIZE:-1}" == "1" ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

"${cc}" "${flags[@]}" \
    -I"${repo_root}/components/vibe_usage/include" \
    -I"${repo_root}/components/vibe_usage/src" \
    -I"${repo_root}/components/app_controller/include" \
    -I"${repo_root}/firmware/zectrix/main" \
    "${repo_root}/components/vibe_usage/src/vibe_usage.c" \
    "${repo_root}/components/vibe_usage/src/vibe_date.c" \
    "${repo_root}/components/vibe_usage/src/vibe_aggregate.c" \
    "${repo_root}/components/vibe_usage/src/vibe_cache.c" \
    "${repo_root}/components/vibe_usage/src/vibe_parser.c" \
    "${repo_root}/components/vibe_usage/src/vibe_state.c" \
    "${repo_root}/components/vibe_usage/src/vibe_http_policy.c" \
    "${repo_root}/components/vibe_usage/src/vibe_json_safety.c" \
    "${repo_root}/components/vibe_usage/src/vibe_device_flow_policy.c" \
    "${repo_root}/firmware/zectrix/main/epd_refresh_policy.c" \
    "${repo_root}/tests/host/test_core.c" \
    -o "${test_dir}/test_core"

"${test_dir}/test_core" "${repo_root}/tests/fixtures/usage-day.json"

"${cc}" "${flags[@]}" -I"${repo_root}/components/vibe_ui/include" \
    "${repo_root}/components/vibe_ui/vibe_i18n.c" \
    "${repo_root}/components/vibe_ui/generated/vibe_messages.c" \
    "${repo_root}/components/vibe_ui/generated/vibe_glyphs.c" \
    "${repo_root}/tests/host/test_i18n.c" -o "${test_dir}/test_i18n"
"${test_dir}/test_i18n"

"${cc}" "${flags[@]}" -I"${repo_root}/tests/host/config_stubs" \
    -I"${repo_root}/components/device_config/include" \
    -I"${repo_root}/components/vibe_ui/include" \
    -I"${repo_root}/components/vibe_usage/include" \
    "${repo_root}/components/device_config/device_config.c" \
    "${repo_root}/components/vibe_usage/src/vibe_cache.c" \
    "${repo_root}/components/vibe_usage/src/vibe_date.c" \
    "${repo_root}/components/vibe_usage/src/vibe_aggregate.c" \
    "${repo_root}/tests/host/test_config.c" -o "${test_dir}/test_config"
"${test_dir}/test_config"

for unit in vibe_i18n generated/vibe_messages generated/vibe_glyphs; do
    "${cc}" "${flags[@]}" -I"${repo_root}/components/vibe_ui/include" \
        -c "${repo_root}/components/vibe_ui/${unit}.c" \
        -o "${test_dir}/${unit##*/}.o"
done
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"${repo_root}/tests/host/config_stubs" \
    -I"${repo_root}/components/app_controller/include" \
    -I"${repo_root}/components/vibe_ui/include" \
    -I"${repo_root}/firmware/zectrix/components/zectrix_canvas/include" \
    -I"${repo_root}/firmware/zectrix/components/zectrix_canvas/font" \
    "${repo_root}/firmware/zectrix/components/zectrix_canvas/zectrix_canvas.cc" \
    "${repo_root}/tests/host/test_note_i18n.cc" \
    "${test_dir}/vibe_i18n.o" "${test_dir}/vibe_messages.o" "${test_dir}/vibe_glyphs.o" \
    -o "${test_dir}/test_note_i18n"
"${test_dir}/test_note_i18n"
