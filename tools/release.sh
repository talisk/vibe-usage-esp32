#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-}"
output_root="${2:-${repo_root}/dist}"

if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+([+-][0-9A-Za-z.-]+)?$ ]]; then
    echo "Usage: $0 VERSION [output-directory]" >&2
    exit 2
fi
release_dir="${output_root}/v${version}"
work_dir="$(mktemp -d /tmp/vibe-usage-release.XXXXXX)"
stage_dir=""
cleanup() {
    case "${work_dir}" in
        /tmp/vibe-usage-release.*) rm -rf -- "${work_dir}" ;;
    esac
    if [[ -n "${stage_dir}" ]]; then
        case "${stage_dir}" in
            "${output_root}"/.vibe-usage-release.*) rm -rf -- "${stage_dir}" ;;
        esac
    fi
}
trap cleanup EXIT

project_version="$(<"${repo_root}/VERSION")"
if [[ "${version}" != "${project_version}" ]]; then
    echo "ERROR: release version must match the repository VERSION file." >&2
    exit 1
fi

"${repo_root}/tools/validate.sh" --static
"${repo_root}/tools/build.sh" all "${work_dir}/build"
python3 "${repo_root}/tools/check_stack_usage.py" \
    "${work_dir}/build/passport" "${work_dir}/build/note4"

mkdir -p -- "${output_root}"
output_root="$(cd -- "${output_root}" && pwd)"
release_dir="${output_root}/v${version}"
if [[ -e "${release_dir}" ]]; then
    echo "ERROR: release directory already exists: ${release_dir}" >&2
    exit 1
fi
stage_dir="$(mktemp -d "${output_root}/.vibe-usage-release.XXXXXX")"

mkdir -p -- \
    "${stage_dir}/passport/bootloader" \
    "${stage_dir}/passport/partition_table" \
    "${stage_dir}/note4-black-white/bootloader" \
    "${stage_dir}/note4-black-white/partition_table" \
    "${stage_dir}/tests/fixtures" \
    "${stage_dir}/tools"

install -m 0644 "${work_dir}/build/passport/bootloader/bootloader.bin" \
    "${stage_dir}/passport/bootloader/bootloader.bin"
install -m 0644 "${work_dir}/build/passport/partition_table/partition-table.bin" \
    "${stage_dir}/passport/partition_table/partition-table.bin"
install -m 0644 "${work_dir}/build/passport/vibe_usage_ai_passport.bin" \
    "${stage_dir}/passport/vibe_usage_ai_passport.bin"
install -m 0644 "${work_dir}/build/passport/FoloToy-AI-Passport-full.bin" \
    "${stage_dir}/passport/FoloToy-AI-Passport-full.bin"
install -m 0644 "${work_dir}/build/passport/flash-plan.json" \
    "${stage_dir}/passport/flash-plan.json"
install -m 0644 "${work_dir}/build/passport/flasher_args.json" \
    "${stage_dir}/passport/flasher_args.json"

install -m 0644 "${work_dir}/build/note4/bootloader/bootloader.bin" \
    "${stage_dir}/note4-black-white/bootloader/bootloader.bin"
install -m 0644 "${work_dir}/build/note4/partition_table/partition-table.bin" \
    "${stage_dir}/note4-black-white/partition_table/partition-table.bin"
install -m 0644 "${work_dir}/build/note4/vibe_usage_zectrix.bin" \
    "${stage_dir}/note4-black-white/vibe_usage_zectrix.bin"
install -m 0644 "${work_dir}/build/note4/vibe-usage-note4-black-white-full.bin" \
    "${stage_dir}/note4-black-white/vibe-usage-note4-black-white-full.bin"
install -m 0644 "${work_dir}/build/note4/flash-plan.json" \
    "${stage_dir}/note4-black-white/flash-plan.json"
install -m 0644 "${work_dir}/build/note4/flasher_args.json" \
    "${stage_dir}/note4-black-white/flasher_args.json"

install -m 0644 "${repo_root}/docs/installation.md" \
    "${stage_dir}/INSTALLATION.md"
install -m 0644 "${repo_root}/README.md" "${stage_dir}/README.md"
install -m 0644 "${repo_root}/README.zh_CN.md" \
    "${stage_dir}/README.zh_CN.md"
install -m 0644 "${repo_root}/SECURITY.md" "${stage_dir}/SECURITY.md"
install -m 0644 "${repo_root}/THIRD_PARTY_NOTICES.md" \
    "${stage_dir}/THIRD_PARTY_NOTICES.md"
install -m 0644 "${repo_root}/LICENSE" "${stage_dir}/LICENSE"
install -m 0644 "${repo_root}/VERSION" "${stage_dir}/VERSION"
for document in AGENTS.md CHANGELOG.md CODE_OF_CONDUCT.md \
                CONTRIBUTING.md SUPPORT.md; do
    install -m 0644 "${repo_root}/${document}" "${stage_dir}/${document}"
done
cp -R "${repo_root}/docs" "${stage_dir}/docs"
mkdir -p "${stage_dir}/.agents/skills/flash-firmware"
install -m 0644 "${repo_root}/.agents/skills/flash-firmware/SKILL.md" \
    "${stage_dir}/.agents/skills/flash-firmware/SKILL.md"
cp -R "${repo_root}/licenses" "${stage_dir}/licenses"
cp -R "${repo_root}/tests/fixtures/." "${stage_dir}/tests/fixtures/"
install -m 0755 "${repo_root}/tools/flash.sh" "${stage_dir}/tools/flash.sh"
install -m 0755 "${repo_root}/tools/verify_firmware.py" \
    "${stage_dir}/tools/verify_firmware.py"

python3 "${repo_root}/tools/make_release_metadata.py" \
    --version "${version}" --release-dir "${stage_dir}" \
    --repo-root "${repo_root}"
python3 "${repo_root}/tools/check_artifact_privacy.py" "${stage_dir}"
if command -v sha256sum >/dev/null 2>&1; then
    (cd -- "${stage_dir}" && sha256sum --check SHA256SUMS)
else
    (cd -- "${stage_dir}" && shasum -a 256 --check SHA256SUMS)
fi
mv -- "${stage_dir}" "${release_dir}"
stage_dir=""
echo "Release package: ${release_dir}"
