#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d /tmp/vibe-smart-llm.XXXXXX)"
trap 'case "${test_dir}" in /tmp/vibe-smart-llm.*) rm -rf -- "${test_dir}" ;; esac' EXIT
flags=(-std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
"${CC:-cc}" "${flags[@]}" -Wno-deprecated-declarations \
    -I"${repo_root}/tests/host/vendor/cjson" \
    -c "${repo_root}/tests/host/vendor/cjson/cJSON.c" -o "${test_dir}/cJSON.o"
"${CC:-cc}" "${flags[@]}" \
    -I"${repo_root}/tests/host/llm_stubs" \
    -I"${repo_root}/tests/host/vendor/cjson" \
    -I"${repo_root}/components/llm_portal/tests/stubs" \
    -I"${repo_root}/components/llm_portal/tests" \
    -I"${repo_root}/components/llm_portal/include" \
    -I"${repo_root}/components/smart_todo/include" \
    -I"${repo_root}/components/board_services/include" \
    "${test_dir}/cJSON.o" \
    "${repo_root}/components/llm_portal/llm_endpoint.c" \
    "${repo_root}/components/llm_portal/llm_settings.c" \
    "${repo_root}/components/llm_portal/tests/nvs_fixture.c" \
    "${repo_root}/components/smart_todo/smart_todo.c" \
    "${repo_root}/components/smart_todo/smart_todo_llm.c" \
    "${repo_root}/tests/host/test_smart_llm.c" -lm -o "${test_dir}/test_smart_llm"
"${test_dir}/test_smart_llm"
