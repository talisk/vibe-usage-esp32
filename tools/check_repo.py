#!/usr/bin/env python3
"""Check repository safety contracts, privacy, links, and release hygiene."""

from __future__ import annotations

import csv
import json
import os
import re
import stat
import subprocess
import sys
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple


ROOT = Path(__file__).resolve().parents[1]
REQUIRED_FILES = (
    "README.md",
    "README.zh_CN.md",
    "AGENTS.md",
    ".agents/skills/flash-firmware/SKILL.md",
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "CONTRIBUTING.md",
    "CODE_OF_CONDUCT.md",
    "SECURITY.md",
    "SUPPORT.md",
    "CHANGELOG.md",
    "docs/architecture.md",
    "docs/installation.md",
    "docs/upstream-lock.md",
    "docs/core-sync.md",
    "docs/adr/0001-streaming-parser.md",
    "docs/acceptance/2026-09-05-initial-bringup.md",
    "docs/acceptance/2026-09-05-authenticated-bringup.md",
    "docs/acceptance/2026-09-05-ui-fixes.md",
    "tools/check_artifact_privacy.py",
    ".github/workflows/ci.yml",
    ".github/workflows/release.yml",
)
UPSTREAM_REVISIONS = {
    "FoloToy/ai-passport": "f913af29a387f8983ef4faa9f1580b14e88b0740",
    "itopinion/zectrix-note4-epd-demo":
        "ca285c98ed0641f86780edb1f5ec77b0335fe649",
    "78/esp-wifi-connect": "347682fa013b52f863052ad4b1a793ca3cabff17",
}
PINNED_ACTIONS = {
    "actions/checkout": "d23441a48e516b6c34aea4fa41551a30e30af803",
    "actions/cache": "caa296126883cff596d87d8935842f9db880ef25",
    "espressif/esp-idf-ci-action":
        "9d38657f3d789ca759b2b37aaf5ceffbc42c4f0d",
    "actions/upload-artifact":
        "043fb46d1a93c77aae656e7c1c64a875d1fc6a0a",
    "actions/download-artifact":
        "3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c",
    "softprops/action-gh-release":
        "3bb12739c298aeb8a4eeaf626c5b8d85266b0e65",
}
SECRET_PATTERNS = (
    ("private key", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----")),
    ("GitHub token", re.compile(r"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{30,}\b")),
    ("GitHub fine-grained token", re.compile(r"\bgithub_pat_[A-Za-z0-9_]{30,}\b")),
    ("AWS access key", re.compile(r"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")),
    ("Vibe credential", re.compile(r"\bvbu_[A-Za-z0-9_-]{20,}\b")),
    (
        "literal Bearer credential",
        re.compile(r"\bBearer[ \t]+(?!credentials?\b|token\b|values?\b)[A-Za-z0-9._~+/-]{20,}"),
    ),
)
MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
ACTION_USE = re.compile(r"\buses:\s*([^\s@]+)@([^\s#]+)")


def repository_files() -> List[Path]:
    result = subprocess.run(
        [
            "git",
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "-z",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return [ROOT / os.fsdecode(value) for value in result.stdout.split(b"\0") if value]


def text_files(paths: Iterable[Path]) -> Iterable[Tuple[Path, str]]:
    for path in paths:
        if not path.is_file():
            continue
        try:
            raw = path.read_bytes()
        except OSError:
            continue
        if b"\0" in raw:
            continue
        try:
            yield path, raw.decode("utf-8")
        except UnicodeDecodeError:
            continue


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def check_required(errors: List[str]) -> None:
    for name in REQUIRED_FILES:
        if not (ROOT / name).is_file():
            errors.append("missing required file: {}".format(name))


def check_version(errors: List[str]) -> None:
    version_path = ROOT / "VERSION"
    if not version_path.is_file() or not re.fullmatch(
        r"[0-9]+\.[0-9]+\.[0-9]+(?:[+-][0-9A-Za-z.-]+)?\n?",
        version_path.read_text(encoding="utf-8")
    ):
        errors.append("VERSION must contain one semantic version")
    for board in ("ai-passport", "zectrix"):
        cmake = (ROOT / "firmware" / board / "CMakeLists.txt").read_text()
        if 'file(STRINGS "${VIBE_REPOSITORY_ROOT}/VERSION" PROJECT_VER LIMIT_COUNT 1)' not in cmake:
            errors.append("{} must use the shared VERSION".format(board))
    http = (ROOT / "components/vibe_usage/src/vibe_http.c").read_text()
    if 'esp_app_get_description()->version' not in http or re.search(r'vibe-usage-esp32/\d', http):
        errors.append("HTTP User-Agent must use the built application version")


def check_inventory(paths: Sequence[Path], errors: List[str]) -> None:
    forbidden_names = {".env", "sdkconfig"}
    forbidden_suffixes = {".bin", ".elf", ".map", ".pyc", ".su"}
    for path in paths:
        name = relative(path)
        if (
            path.name in forbidden_names
            or path.suffix.lower() in forbidden_suffixes
            or "__pycache__" in path.parts
            or "/managed_components/" in "/{}".format(name)
            or name.startswith(("build/", "dist/"))
        ):
            errors.append("generated or private file would be committed: {}".format(name))


def check_text(paths: Sequence[Path], errors: List[str]) -> None:
    for path, content in text_files(paths):
        name = relative(path)
        if (
            path != Path(__file__).resolve()
            and "<<<<<<< " in content
            and ">>>>>>> " in content
        ):
            errors.append("merge conflict marker: {}".format(name))
        for label, pattern in SECRET_PATTERNS:
            if pattern.search(content):
                errors.append("possible {} in {}".format(label, name))


def clean_link_target(raw: str) -> str:
    value = raw.strip()
    if value.startswith("<") and ">" in value:
        return value[1 : value.index(">")]
    return value.split(None, 1)[0]


def check_markdown_links(paths: Sequence[Path], errors: List[str]) -> None:
    for path, content in text_files(paths):
        if path.suffix.lower() != ".md":
            continue
        prose = "\n".join(content.split("```", 2)[::2])
        if content.count("```") > 2:
            prose = "\n".join(content.split("```")[::2])
        prose = re.sub(r"`[^`\n]*`", "", prose)
        for match in MARKDOWN_LINK.finditer(prose):
            target = clean_link_target(match.group(1))
            if not target or target.startswith(
                ("#", "http://", "https://", "mailto:", "codex://")
            ):
                continue
            file_part = target.split("#", 1)[0]
            candidate = (path.parent / file_part).resolve()
            try:
                candidate.relative_to(ROOT)
            except ValueError:
                errors.append(
                    "local link escapes repository in {}: {}".format(
                        relative(path), target
                    )
                )
                continue
            if not candidate.exists():
                errors.append(
                    "broken local link in {}: {}".format(relative(path), target)
                )


def partition_rows(path: Path) -> List[Tuple[str, str, str, int, int]]:
    rows: List[Tuple[str, str, str, int, int]] = []
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.reader(
            line for line in stream if line.strip() and not line.lstrip().startswith("#")
        ):
            if len(row) < 5:
                raise ValueError("invalid partition row in {}".format(relative(path)))
            rows.append(
                (
                    row[0].strip(),
                    row[1].strip(),
                    row[2].strip(),
                    int(row[3].strip(), 0),
                    int(row[4].strip(), 0),
                )
            )
    return rows


def check_partitions(errors: List[str]) -> None:
    expected_common = [
        ("nvs", "data", "nvs", 0x9000, 0x6000),
        ("phy_init", "data", "phy", 0xF000, 0x1000),
        ("factory", "app", "factory", 0x10000, 0x300000),
    ]
    expected_passport = expected_common + [
        ("cardid", "data", "nvs", 0x356000, 0x4000),
        ("recovery", "app", "test", 0x700000, 0x100000),
    ]
    try:
        passport = partition_rows(ROOT / "firmware/ai-passport/partitions.csv")
        note4 = partition_rows(ROOT / "firmware/zectrix/partitions.csv")
    except (OSError, ValueError) as error:
        errors.append(str(error))
        return
    if passport != expected_passport:
        errors.append("Passport partition CSV violates the protected layout")
    if note4 != expected_common:
        errors.append("NOTE4 partition CSV differs from the fixed P0 layout")


def check_upstreams(errors: List[str]) -> None:
    try:
        content = (ROOT / "docs/upstream-lock.md").read_text(encoding="utf-8")
    except OSError as error:
        errors.append("cannot read upstream lock: {}".format(error))
        return
    for project, revision in UPSTREAM_REVISIONS.items():
        if project not in content or revision not in content:
            errors.append("upstream lock is missing {}@{}".format(project, revision))


def check_actions(errors: List[str]) -> None:
    workflows = sorted((ROOT / ".github/workflows").glob("*.yml"))
    for path in workflows:
        content = path.read_text(encoding="utf-8")
        for action, revision in ACTION_USE.findall(content):
            expected = PINNED_ACTIONS.get(action)
            if expected is None:
                errors.append(
                    "unapproved GitHub Action in {}: {}".format(relative(path), action)
                )
            elif revision != expected:
                errors.append(
                    "GitHub Action is not pinned to the approved SHA in {}: {}@{}".format(
                        relative(path), action, revision
                    )
                )


def check_contract_fixture(errors: List[str]) -> None:
    path = ROOT / "tests/fixtures/contract-meta.json"
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        errors.append("invalid contract metadata: {}".format(error))
        return
    expected = {
        "schemaVersion": 1,
        "serviceOrigin": "https://vibecafe.ai",
        "metricId": "API_TOTAL_V1",
    }
    for key, wanted in expected.items():
        if value.get(key) != wanted:
            errors.append("contract metadata {} must be {!r}".format(key, wanted))
    if "No credential" not in str(value.get("privacy", "")):
        errors.append("contract metadata lacks an explicit privacy statement")


def check_destructive_calls(errors: List[str]) -> None:
    patterns = (
        re.compile(r"\bnvs_flash_erase\s*\("),
        re.compile(r"\bidf\.py\b[^\n]*\berase-flash\b"),
        re.compile(r"\besptool(?:\.py)?\b[^\n]*\berase_flash\b"),
    )
    roots = (ROOT / "components", ROOT / "firmware", ROOT / "tools")
    for base in roots:
        for path in base.rglob("*"):
            if not path.is_file() or path == Path(__file__).resolve():
                continue
            if path.suffix.lower() not in {".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".sh"}:
                continue
            if any(
                part == "managed_components" or part.startswith("build")
                for part in path.parts
            ):
                continue
            try:
                content = path.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError):
                continue
            if any(pattern.search(content) for pattern in patterns):
                errors.append("destructive flash/NVS operation in {}".format(relative(path)))


def check_executable_tools(errors: List[str]) -> None:
    for path in sorted((ROOT / "tools").iterdir()):
        if path.is_file() and path.suffix in {".py", ".sh"}:
            mode = path.stat().st_mode
            if not mode & stat.S_IXUSR:
                errors.append("tool is not executable: {}".format(relative(path)))


def main() -> int:
    errors: List[str] = []
    try:
        paths = repository_files()
    except subprocess.CalledProcessError as error:
        print("ERROR: cannot enumerate repository files: {}".format(error), file=sys.stderr)
        return 1
    check_required(errors)
    check_version(errors)
    check_inventory(paths, errors)
    check_text(paths, errors)
    check_markdown_links(paths, errors)
    check_partitions(errors)
    check_upstreams(errors)
    check_actions(errors)
    check_contract_fixture(errors)
    check_destructive_calls(errors)
    check_executable_tools(errors)
    if errors:
        for error in sorted(set(errors)):
            print("ERROR: {}".format(error), file=sys.stderr)
        return 1
    print("Repository contracts: PASS ({} source files checked)".format(len(paths)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
