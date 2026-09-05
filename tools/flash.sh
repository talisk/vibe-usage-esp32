#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
board="${1:-}"
port="${2:-}"
build_dir="${3:-}"
confirmation="${4:-}"

usage() {
    echo "Usage: $0 passport|note4 PORT [build-directory] [--yes|--dry-run]" >&2
}

case "${board}" in
    passport)
        chip="esp32c3"
        flash_size="8MB"
        app_name="vibe_usage_ai_passport.bin"
        default_build="${repo_root}/build/firmware/passport"
        ;;
    note4)
        chip="esp32s3"
        flash_size="16MB"
        app_name="vibe_usage_zectrix.bin"
        default_build="${repo_root}/build/firmware/note4"
        ;;
    *)
        usage
        exit 2
        ;;
esac

if [[ -z "${port}" ]]; then
    usage
    exit 2
fi
if [[ -z "${build_dir}" || "${build_dir}" == "--yes" || "${build_dir}" == "--dry-run" ]]; then
    confirmation="${build_dir:-${confirmation}}"
    build_dir="${default_build}"
fi
if [[ "${confirmation}" != "--yes" && "${confirmation}" != "--dry-run" ]]; then
    echo "ERROR: pass --dry-run to inspect or --yes to authorize this exact write." >&2
    exit 2
fi

python3 "${repo_root}/tools/verify_firmware.py" \
    "${board}" "${build_dir}" --require-merged

flash_command=(
    python3 -m esptool
    --chip "${chip}"
    --port "${port}"
    --baud 460800
    write_flash
    --flash_mode dio
    --flash_freq 80m
    --flash_size "${flash_size}"
    0x0 "${build_dir}/bootloader/bootloader.bin"
    0x8000 "${build_dir}/partition_table/partition-table.bin"
    0x10000 "${build_dir}/${app_name}"
)

if [[ "${confirmation}" == "--dry-run" ]]; then
    printf 'Dry run only; no serial port was opened. Planned command:\n'
    printf ' %q' "${flash_command[@]}"
    printf '\n'
    exit 0
fi

if [[ ! -c "${port}" ]]; then
    echo "ERROR: serial port is not a character device: ${port}" >&2
    exit 1
fi
if ! python3 -m esptool version >/dev/null 2>&1; then
    echo "ERROR: Python esptool is unavailable; activate ESP-IDF 5.5.3." >&2
    exit 1
fi

chip_output="$(python3 -m esptool --chip "${chip}" --port "${port}" chip_id 2>&1)" || {
    printf '%s\n' "${chip_output}" >&2
    echo "ERROR: read-only chip identification failed; nothing was written." >&2
    exit 1
}
case "${board}:${chip_output}" in
    passport:*ESP32-C3*) ;;
    note4:*ESP32-S3*) ;;
    *)
        printf '%s\n' "${chip_output}" >&2
        echo "ERROR: the connected chip does not match ${board}; nothing was written." >&2
        exit 1
        ;;
esac

flash_output="$(python3 -m esptool --chip "${chip}" --port "${port}" flash_id 2>&1)" || {
    printf '%s\n' "${flash_output}" >&2
    echo "ERROR: read-only flash identification failed; nothing was written." >&2
    exit 1
}
if [[ "${flash_output}" != *"${flash_size}"* ]]; then
    printf '%s\n' "${flash_output}" >&2
    echo "ERROR: detected flash does not confirm ${flash_size}; nothing was written." >&2
    exit 1
fi

echo "Hardware identity: PASS (${chip}, ${flash_size}, ${port})"
echo "Writing only bootloader, partition table, and factory app segments."
"${flash_command[@]}"
echo "Flash write: PASS (${board})"
