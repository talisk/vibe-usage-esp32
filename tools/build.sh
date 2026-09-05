#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
board="${1:-all}"
build_root="${2:-${repo_root}/build/firmware}"

usage() {
    echo "Usage: $0 [passport|note4|all] [build-root]" >&2
}

if [[ "${board}" != "passport" && "${board}" != "note4" && "${board}" != "all" ]]; then
    usage
    exit 2
fi
if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py is unavailable; activate ESP-IDF 5.5.3 first." >&2
    exit 1
fi
idf_version="$(idf.py --version 2>&1)"
if [[ ! "${idf_version}" =~ 5\.5\.3 ]]; then
    echo "ERROR: ESP-IDF 5.5.3 is required; found ${idf_version}." >&2
    exit 1
fi

mkdir -p -- "${build_root}"
build_root="$(cd -- "${build_root}" && pwd)"

build_one() (
    local name="$1"
    local project_dir
    local build_dir
    local merged_name

    case "${name}" in
        passport)
            project_dir="${repo_root}/firmware/ai-passport"
            merged_name="FoloToy-AI-Passport-full.bin"
            ;;
        note4)
            project_dir="${repo_root}/firmware/zectrix"
            merged_name="vibe-usage-note4-black-white-full.bin"
            ;;
    esac
    build_dir="${build_root}/${name}"
    mkdir -p -- "${build_dir}"
    defaults_stamp="${build_dir}/.vibe-sdkconfig.defaults"
    # The build directory is generated state. Reseed SDKCONFIG whenever the
    # checked-in defaults change, while keeping unchanged incremental builds
    # fast. The layout verifier still rejects manual config drift.
    if [[ ! -f "${build_dir}/sdkconfig" || ! -f "${defaults_stamp}" ]] ||
       ! cmp -s "${project_dir}/sdkconfig.defaults" "${defaults_stamp}"; then
        install -m 0644 "${project_dir}/sdkconfig.defaults" \
            "${build_dir}/sdkconfig"
        install -m 0644 "${project_dir}/sdkconfig.defaults" \
            "${defaults_stamp}"
    fi
    cd -- "${project_dir}"
    SDKCONFIG_DEFAULTS="${project_dir}/sdkconfig.defaults" \
        idf.py -B "${build_dir}" \
        -D "SDKCONFIG=${build_dir}/sdkconfig" build
    idf.py -B "${build_dir}" merge-bin \
        -o "${build_dir}/${merged_name}"
    python3 "${repo_root}/tools/verify_firmware.py" \
        "${name}" "${build_dir}" --require-merged \
        --plan "${build_dir}/flash-plan.json"
)

if [[ "${board}" == "all" ]]; then
    build_one passport
    build_one note4
else
    build_one "${board}"
fi
