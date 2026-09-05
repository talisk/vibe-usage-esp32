#!/usr/bin/env python3
"""Enforce stack-frame budgets for the product-owned hot paths."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, Iterable, Tuple


LIMITS = {
    "vibe_cache_decode": 256,
    "decode_cache_into": 256,
    "fetch_today": 4096,
    "reconcile_one": 4096,
    "poll_device_link": 4096,
}


def records(build_dir: Path) -> Iterable[Tuple[str, int, Path]]:
    for path in build_dir.glob("esp-idf/**/*.su"):
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError):
            continue
        for line in lines:
            fields = line.split("\t")
            if len(fields) < 2:
                continue
            try:
                size = int(fields[1])
            except ValueError:
                continue
            function = fields[0].rsplit(":", 1)[-1]
            yield function, size, path


def check(build_dirs: Iterable[Path]) -> None:
    seen: Dict[str, int] = {}
    violations = []
    for build_dir in build_dirs:
        if not build_dir.is_dir():
            raise ValueError("missing build directory {}".format(build_dir))
        for function, size, path in records(build_dir):
            if function not in LIMITS:
                continue
            seen[function] = max(seen.get(function, 0), size)
            if size > LIMITS[function]:
                violations.append(
                    "{}: {} uses {} bytes (limit {})".format(
                        path, function, size, LIMITS[function]
                    )
                )
    missing = sorted(set(LIMITS) - set(seen))
    if missing:
        violations.append("missing stack records: {}".format(", ".join(missing)))
    if violations:
        raise ValueError("; ".join(violations))
    print(
        "Stack frame budgets: PASS ({})".format(
            ", ".join(
                "{}={}B".format(name, seen[name]) for name in sorted(seen)
            )
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        check(path.resolve() for path in args.build_dir)
    except ValueError as error:
        print("ERROR: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
