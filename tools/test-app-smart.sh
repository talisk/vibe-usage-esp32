#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d /tmp/vibe-app-smart.XXXXXX)"
trap 'case "${test_dir}" in /tmp/vibe-app-smart.*) rm -rf -- "${test_dir}" ;; esac' EXIT
flags=(-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
"${CC:-cc}" "${flags[@]}" -Wno-deprecated-declarations \
    -I"${repo_root}/tests/host/vendor/cjson" \
    -c "${repo_root}/tests/host/vendor/cjson/cJSON.c" -o "${test_dir}/cJSON.o"
"${CC:-cc}" "${flags[@]}" \
    -I"${repo_root}/tests/host/app_stubs" -I"${repo_root}/tests/host/vendor/cjson" \
    -I"${repo_root}/components/app_controller/include" \
    -I"${repo_root}/components/device_config/include" -I"${repo_root}/components/vibe_ui/include" \
    -I"${repo_root}/components/vibe_usage/include" -I"${repo_root}/components/wifi_adapter/include" \
    -I"${repo_root}/components/board_services/include" -I"${repo_root}/components/llm_portal/include" \
    -I"${repo_root}/components/smart_todo/include" \
    "${test_dir}/cJSON.o" "${repo_root}/components/smart_todo/smart_todo.c" \
    "${repo_root}/tests/host/test_app_smart_lifecycle.c" -lm -o "${test_dir}/test_app_smart"
"${test_dir}/test_app_smart"
