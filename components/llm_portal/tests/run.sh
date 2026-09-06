#!/usr/bin/env bash
set -euo pipefail
component="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(cd -- "${component}/../.." && pwd)"
test_dir="$(mktemp -d /tmp/vibe-llm-portal-tests.XXXXXX)"
trap 'case "${test_dir}" in /tmp/vibe-llm-portal-tests.*) rm -rf -- "${test_dir}" ;; esac' EXIT
flags=(-std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
includes=(-I"${component}/tests/stubs" -I"${component}/include" -I"${component}/tests")
sources=("${component}/llm_endpoint.c" "${component}/llm_settings.c" "${component}/tests/nvs_fixture.c")
"${CC:-cc}" "${flags[@]}" "${includes[@]}" "${sources[@]}" "${component}/tests/test_settings.c" -o "${test_dir}/test_settings"
"${test_dir}/test_settings"
json_dir="${repo_root}/tests/host/vendor/cjson"
# Suppress the macOS SDK sprintf deprecation only in the unchanged third-party
# source; keep -Werror for this component and its fixtures.
"${CC:-cc}" "${flags[@]}" -Wno-deprecated-declarations -I"${json_dir}" \
    -c "${json_dir}/cJSON.c" -o "${test_dir}/cJSON.o"
"${CC:-cc}" "${flags[@]}" "${includes[@]}" -I"${component}/../wifi_adapter/include" \
    -I"${json_dir}" "${sources[@]}" "${component}/llm_portal.c" \
    "${test_dir}/cJSON.o" "${component}/tests/test_http.c" -o "${test_dir}/test_http"
"${test_dir}/test_http"
node "${component}/tests/test_portal.mjs"
