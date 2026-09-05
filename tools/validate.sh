#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static() {
    python3 "${repo_root}/tools/check_repo.py"
    "${repo_root}/tools/test-host.sh"
    python3 -m unittest discover -s "${repo_root}/tests" \
        -p "test_verify_firmware.py"
    python3 -m unittest discover -s "${repo_root}/tests" \
        -p "test_ui_resources.py"
    python3 -m py_compile "${repo_root}"/tools/*.py \
        "${repo_root}"/tests/*.py
    bash -n "${repo_root}"/tools/*.sh
    if command -v actionlint >/dev/null 2>&1; then
        actionlint -color "${repo_root}"/.github/workflows/*.yml
    else
        echo "Workflow lint: SKIP (actionlint not installed; CI parses workflows)"
    fi
    echo "Static validation: PASS"
}

run_firmware() (
    local validation_root
    validation_root="$(mktemp -d /tmp/vibe-usage-firmware.XXXXXX)"
    trap 'case "${validation_root}" in /tmp/vibe-usage-firmware.*) rm -rf -- "${validation_root}" ;; esac' EXIT
    "${repo_root}/tools/build.sh" all "${validation_root}"
    python3 "${repo_root}/tools/check_stack_usage.py" \
        "${validation_root}/passport" "${validation_root}/note4"
    "${repo_root}/tools/test-ui-layout.sh"
    mkdir -p -- "${repo_root}/build/verified"
    install -m 0644 \
        "${validation_root}/passport/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/verified/FoloToy-AI-Passport-full.bin"
    install -m 0644 \
        "${validation_root}/note4/vibe-usage-note4-black-white-full.bin" \
        "${repo_root}/build/verified/vibe-usage-note4-black-white-full.bin"
    echo "Firmware validation: PASS"
)

cd -- "${repo_root}"
case "${mode}" in
    --all)
        run_static
        run_firmware
        ;;
    --static)
        run_static
        ;;
    --firmware)
        run_firmware
        ;;
    *)
        usage
        exit 2
        ;;
esac
