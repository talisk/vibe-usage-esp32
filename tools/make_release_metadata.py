#!/usr/bin/env python3
"""Create a release manifest, SPDX inventory, and checksums."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
from typing import Dict, List


MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
REQUIRED_RELEASE_FILES = (
    "README.md",
    "README.zh_CN.md",
    "INSTALLATION.md",
    "AGENTS.md",
    ".agents/skills/flash-firmware/SKILL.md",
    "docs/images/vibe-passport-en.jpg",
    "docs/images/vibe-note-en.jpg",
    "docs/images/vibe-passport-zh.jpg",
    "docs/images/vibe-note-zh.jpg",
    "CHANGELOG.md",
    "CONTRIBUTING.md",
    "CODE_OF_CONDUCT.md",
    "SECURITY.md",
    "SUPPORT.md",
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "docs/api-contract.md",
    "docs/architecture.md",
    "docs/installation.md",
    "docs/upstream-lock.md",
    "licenses/FoloToy-ai-passport-LICENSE",
    "licenses/ZECTRIX-note4-demo-LICENSE",
    "licenses/esp-wifi-connect-LICENSE",
    "licenses/Noto-CJK-OFL-1.1.txt",
    "tests/fixtures/contract-meta.json",
    "tests/fixtures/usage-day.json",
    "tools/flash.sh",
    "tools/verify_firmware.py",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_state(repo_root: Path) -> Dict[str, object]:
    revision = "UNCOMMITTED"
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo_root,
            check=True,
            capture_output=True,
            text=True,
        )
        revision = result.stdout.strip()
    except subprocess.CalledProcessError:
        pass
    status = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    )
    return {"revision": revision, "dirty": bool(status.stdout.strip())}


def lock_dependencies(path: Path) -> List[Dict[str, str]]:
    packages: List[Dict[str, str]] = []
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^  ([^ ][^:]*):$", line)
        if match:
            current = match.group(1)
            continue
        version = re.match(r"^    version: ['\"]?([^'\"]+)['\"]?$", line)
        if current and version:
            packages.append({"name": current, "version": version.group(1)})
            current = None
    return packages


def release_files(release_dir: Path) -> List[Path]:
    excluded = {"SHA256SUMS", "manifest.json", "SBOM.spdx.json"}
    return sorted(
        path
        for path in release_dir.rglob("*")
        if path.is_file() and path.name not in excluded
    )


def clean_link_target(raw: str) -> str:
    value = raw.strip()
    if value.startswith("<") and ">" in value:
        return value[1 : value.index(">")]
    return value.split(None, 1)[0]


def validate_release_contents(release_dir: Path, files: List[Path]) -> None:
    missing = [name for name in REQUIRED_RELEASE_FILES
               if not (release_dir / name).is_file()]
    errors = ["missing required release file: {}".format(name)
              for name in missing]
    for path in files:
        if path.suffix.lower() != ".md":
            continue
        content = path.read_text(encoding="utf-8")
        prose = "\n".join(content.split("```")[::2])
        prose = re.sub(r"`[^`\n]*`", "", prose)
        for match in MARKDOWN_LINK.finditer(prose):
            target = clean_link_target(match.group(1))
            if not target or target.startswith(
                ("#", "http://", "https://", "mailto:")
            ):
                continue
            file_part = target.split("#", 1)[0]
            candidate = (path.parent / file_part).resolve()
            try:
                candidate.relative_to(release_dir)
            except ValueError:
                errors.append("release link escapes package in {}: {}".format(
                    path.relative_to(release_dir), target
                ))
                continue
            if not candidate.exists():
                errors.append("broken release link in {}: {}".format(
                    path.relative_to(release_dir), target
                ))
    if errors:
        raise ValueError("; ".join(errors))


def make_manifest(
    version: str, release_dir: Path, repo_root: Path, files: List[Path]
) -> dict:
    return {
        "schemaVersion": 1,
        "project": "vibe-usage-esp32",
        "version": version,
        "source": source_state(repo_root),
        "toolchain": {"espIdf": "5.5.3"},
        "contracts": {
            "cacheSchema": 2,
            "metric": "API_TOTAL_V1",
            "passportFactoryLimit": "0x300000",
            "passportProtectedCardid": "0x356000+0x4000",
            "passportPermanentRecovery": "0x700000+0x100000",
        },
        "boards": [
            {
                "id": "passport",
                "hardware": "FoloToy AI Passport",
                "chip": "esp32c3",
                "flash": "8MB",
            },
            {
                "id": "note4-black-white",
                "hardware": "ZECTRIX NOTE4 BLACK-WHITE 400x300",
                "chip": "esp32s3",
                "flash": "16MB",
            },
        ],
        "files": [
            {
                "path": str(path.relative_to(release_dir)),
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
            for path in files
        ],
    }


def creation_time() -> str:
    source_date_epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if source_date_epoch is not None:
        try:
            timestamp = int(source_date_epoch)
        except ValueError as error:
            raise ValueError("SOURCE_DATE_EPOCH must be an integer") from error
        value = dt.datetime.fromtimestamp(timestamp, tz=dt.timezone.utc)
    else:
        value = dt.datetime.now(dt.timezone.utc)
    return value.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def make_sbom(version: str, repo_root: Path) -> dict:
    dependencies = [{
        "name": "Vibe CJK subsets (derived from Noto Sans CJK JP)",
        "SPDXID": "SPDXRef-Package-vibe-cjk-subsets",
        "versionInfo": "Sans2.004-523d033d6cb47f4a80c58a35753646f5c3608a78",
        "downloadLocation": "https://github.com/notofonts/noto-cjk/tree/523d033d6cb47f4a80c58a35753646f5c3608a78",
        "filesAnalyzed": False,
        "licenseConcluded": "OFL-1.1",
        "licenseDeclared": "OFL-1.1",
        "copyrightText": "Copyright 2014-2021 Adobe",
    }]
    seen = set()
    for board, relative in (
        ("passport", "firmware/ai-passport/dependencies.lock"),
        ("note4", "firmware/zectrix/dependencies.lock"),
    ):
        for dependency in lock_dependencies(repo_root / relative):
            key = (dependency["name"], dependency["version"])
            if key in seen:
                continue
            seen.add(key)
            dependencies.append(
                {
                    "name": dependency["name"],
                    "SPDXID": "SPDXRef-Package-{}".format(
                        re.sub(r"[^A-Za-z0-9.-]", "-", dependency["name"])
                    ),
                    "versionInfo": dependency["version"],
                    "downloadLocation": "NOASSERTION",
                    "filesAnalyzed": False,
                    "licenseConcluded": "NOASSERTION",
                    "licenseDeclared": "NOASSERTION",
                    "supplier": "NOASSERTION",
                    "comment": "Pinned by an ESP-IDF component lock file; used by {}.".format(
                        board
                    ),
                }
            )
    packages = [
        {
            "name": "vibe-usage-esp32",
            "SPDXID": "SPDXRef-Package-vibe-usage-esp32",
            "versionInfo": version,
            "downloadLocation": "NOASSERTION",
            "filesAnalyzed": False,
            "licenseConcluded": "MIT",
            "licenseDeclared": "MIT",
            "supplier": "Organization: vibe-usage-esp32 contributors",
        }
    ] + dependencies
    created = creation_time()
    project_id = "SPDXRef-Package-vibe-usage-esp32"
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "vibe-usage-esp32-{}".format(version),
        "documentNamespace": "https://vibe-usage-esp32.invalid/spdx/{}".format(
            version
        ),
        "creationInfo": {
            "created": created,
            "creators": ["Tool: vibe-usage-esp32-release-metadata"],
        },
        "documentDescribes": [project_id],
        "packages": packages,
        "relationships": [
            {
                "spdxElementId": project_id,
                "relationshipType": "DEPENDS_ON",
                "relatedSpdxElement": dependency["SPDXID"],
            }
            for dependency in dependencies
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--release-dir", required=True, type=Path)
    parser.add_argument("--repo-root", required=True, type=Path)
    args = parser.parse_args()
    release_dir = args.release_dir.resolve()
    repo_root = args.repo_root.resolve()
    files = release_files(release_dir)
    validate_release_contents(release_dir, files)
    manifest = make_manifest(args.version, release_dir, repo_root, files)
    (release_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (release_dir / "SBOM.spdx.json").write_text(
        json.dumps(make_sbom(args.version, repo_root), indent=2, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    checksum_files = sorted(
        path
        for path in release_dir.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS"
    )
    checksums = "".join(
        "{}  {}\n".format(sha256(path), path.relative_to(release_dir))
        for path in checksum_files
    )
    (release_dir / "SHA256SUMS").write_text(checksums, encoding="utf-8")
    print("Release metadata: PASS ({} files)".format(len(checksum_files)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
