#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d /tmp/vibe-smart-todo.XXXXXX)"
trap 'rm -rf -- "${test_dir}"' EXIT
# Keep the pinned upstream source unchanged; only it uses deprecated sprintf.
"${CC:-cc}" -std=c11 -Wno-deprecated-declarations \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"${repo_root}/tests/host/vendor/cjson" \
    -c "${repo_root}/tests/host/vendor/cjson/cJSON.c" -o "${test_dir}/cJSON.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"${repo_root}/tests/host/smart_stubs" \
    -I"${repo_root}/tests/host/vendor/cjson" \
    -I"${repo_root}/components/smart_todo/include" \
    "${test_dir}/cJSON.o" \
    "${repo_root}/components/smart_todo/smart_todo.c" \
    "${repo_root}/components/smart_todo/smart_todo_store.c" \
    "${repo_root}/tests/host/test_smart_todo.c" -lm -o "${test_dir}/test_smart_todo"
"${test_dir}/test_smart_todo"
