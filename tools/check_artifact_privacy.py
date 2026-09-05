#!/usr/bin/env python3
"""Reject release artifacts containing credentials or private build paths."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Tuple


PATTERNS: Tuple[Tuple[str, re.Pattern[bytes]], ...] = (
    (
        "absolute developer path",
        re.compile(rb"(?:^|[^A-Za-z0-9])/(?:Users|home)/[A-Za-z0-9._-]+/"),
    ),
    (
        "macOS temporary build path",
        re.compile(
            rb"(?:^|[^A-Za-z0-9])/(?:private/var/folders|private/tmp|tmp)/"
        ),
    ),
    ("Vibe credential", re.compile(rb"\bvbu_[A-Za-z0-9_-]{20,}\b")),
    (
        "GitHub token",
        re.compile(rb"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{30,}\b"),
    ),
    (
        "GitHub fine-grained token",
        re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{30,}\b"),
    ),
    ("AWS access key", re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")),
    (
        "literal Bearer credential",
        re.compile(
            rb"\bBearer[ \t]+(?!credentials?\b|token\b|values?\b)"
            rb"[A-Za-z0-9._~+/-]{20,}"
        ),
    ),
    (
        "PEM private key",
        re.compile(
            rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\r?\n"
            rb"(?:[A-Za-z0-9+/]{16,}={0,2}\r?\n)+"
            rb"-----END (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
            re.MULTILINE,
        ),
    ),
)


def files_under(root: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_file() and not path.is_symlink():
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_root", type=Path)
    args = parser.parse_args()
    root = args.artifact_root.resolve()
    if not root.is_dir():
        print("ERROR: artifact root is not a directory", file=sys.stderr)
        return 2

    findings = []
    checked = 0
    for path in files_under(root):
        checked += 1
        try:
            data = path.read_bytes()
        except OSError as error:
            findings.append((path, "unreadable artifact: {}".format(error)))
            continue
        for label, pattern in PATTERNS:
            if pattern.search(data):
                findings.append((path, label))

    if findings:
        for path, label in findings:
            print(
                "ERROR: {} in {}".format(label, path.relative_to(root)),
                file=sys.stderr,
            )
        return 1
    print("Artifact privacy: PASS ({} files checked)".format(checked))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
