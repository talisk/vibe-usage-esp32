#!/usr/bin/env python3
"""Verify build metadata, partition contracts, and merged firmware images."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple


PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0xC00
FACTORY_OFFSET = 0x10000
FACTORY_SIZE = 0x300000
CARDID_OFFSET = 0x356000
CARDID_SIZE = 0x4000
RECOVERY_OFFSET = 0x700000
RECOVERY_SIZE = 0x100000
ENTRY = struct.Struct("<HBBII16sI")
RECOVERY_BOOT_MARKER = b"UP held: booting permanent recovery"


@dataclass(frozen=True)
class BoardSpec:
    name: str
    chip: str
    flash_size_text: str
    flash_size: int
    app_name: str
    merged_name: str
    protected: bool


SPECS = {
    "passport": BoardSpec(
        "FoloToy AI Passport",
        "esp32c3",
        "8MB",
        8 * 1024 * 1024,
        "vibe_usage_ai_passport.bin",
        "FoloToy-AI-Passport-full.bin",
        True,
    ),
    "note4": BoardSpec(
        "ZECTRIX NOTE4 BLACK-WHITE",
        "esp32s3",
        "16MB",
        16 * 1024 * 1024,
        "vibe_usage_zectrix.bin",
        "vibe-usage-note4-black-white-full.bin",
        False,
    ),
}


@dataclass(frozen=True)
class Partition:
    kind: int
    subtype: int
    offset: int
    size: int
    label: str

    @property
    def end(self) -> int:
        return self.offset + self.size


def parse_partition_table(raw: bytes) -> Tuple[List[Partition], bool]:
    """Parse an ESP-IDF partition table and verify its MD5 marker."""
    if len(raw) < PARTITION_TABLE_SIZE:
        raise ValueError("partition table is truncated")

    partitions: List[Partition] = []
    found_md5 = False
    for cursor in range(0, PARTITION_TABLE_SIZE, ENTRY.size):
        magic = int.from_bytes(raw[cursor : cursor + 2], "little")
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:
            expected = hashlib.md5(raw[:cursor]).digest()
            actual = raw[cursor + 16 : cursor + 32]
            if actual != expected:
                raise ValueError("partition table MD5 marker does not match")
            found_md5 = True
            break
        if magic != 0x50AA:
            raise ValueError(
                "invalid partition entry at table offset 0x{:x}".format(cursor)
            )

        _, kind, subtype, offset, size, label_raw, _ = ENTRY.unpack_from(
            raw, cursor
        )
        label = label_raw.split(b"\0", 1)[0].decode("ascii", "strict")
        if not label or not size or offset < 0x9000:
            raise ValueError("invalid partition bounds for {!r}".format(label))
        partitions.append(Partition(kind, subtype, offset, size, label))

    if not partitions:
        raise ValueError("partition table is empty")
    return partitions, found_md5


def _require_partition(
    by_label: Dict[str, Partition], expected: Partition
) -> None:
    actual = by_label.get(expected.label)
    if actual != expected:
        raise ValueError(
            "partition {!r} must remain {}, got {}".format(
                expected.label, expected, actual
            )
        )


def verify_partition_contract(
    partitions: List[Partition], found_md5: bool, spec: BoardSpec
) -> None:
    if not found_md5:
        raise ValueError("partition table has no MD5 marker")
    by_label = {item.label: item for item in partitions}
    if len(by_label) != len(partitions):
        raise ValueError("partition labels must be unique")

    _require_partition(
        by_label, Partition(0, 0, FACTORY_OFFSET, FACTORY_SIZE, "factory")
    )
    _require_partition(by_label, Partition(1, 2, 0x9000, 0x6000, "nvs"))
    _require_partition(by_label, Partition(1, 1, 0xF000, 0x1000, "phy_init"))
    if spec.protected:
        _require_partition(
            by_label,
            Partition(1, 2, CARDID_OFFSET, CARDID_SIZE, "cardid"),
        )
        _require_partition(
            by_label,
            Partition(0, 0x20, RECOVERY_OFFSET, RECOVERY_SIZE, "recovery"),
        )
    elif set(by_label) != {"nvs", "phy_init", "factory"}:
        raise ValueError("NOTE4 partition table contains an unexpected partition")

    ordered = sorted(partitions, key=lambda item: item.offset)
    for item in ordered:
        if item.end > spec.flash_size:
            raise ValueError("partition {!r} exceeds physical flash".format(item.label))
    for left, right in zip(ordered, ordered[1:]):
        if left.end > right.offset:
            raise ValueError(
                "partitions {!r} and {!r} overlap".format(
                    left.label, right.label
                )
            )

    if spec.protected:
        for item in partitions:
            if (
                item.label != "cardid"
                and item.offset < CARDID_OFFSET + CARDID_SIZE
                and CARDID_OFFSET < item.end
            ):
                raise ValueError(
                    "partition {!r} overlaps protected cardid".format(item.label)
                )
            if (
                item.label != "recovery"
                and item.offset < RECOVERY_OFFSET + RECOVERY_SIZE
                and RECOVERY_OFFSET < item.end
            ):
                raise ValueError(
                    "partition {!r} overlaps permanent Recovery".format(
                        item.label
                    )
                )


def _read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("cannot read {}: {}".format(path, error))
    if not isinstance(value, dict):
        raise ValueError("{} must contain a JSON object".format(path))
    return value


def _flash_files(metadata: dict) -> Dict[int, str]:
    raw = metadata.get("flash_files")
    if not isinstance(raw, dict):
        raise ValueError("flasher_args.json has no flash_files object")
    result: Dict[int, str] = {}
    for raw_offset, raw_name in raw.items():
        if not isinstance(raw_offset, str) or not isinstance(raw_name, str):
            raise ValueError("flasher_args.json has invalid flash_files values")
        try:
            offset = int(raw_offset, 0)
        except ValueError as error:
            raise ValueError("invalid flash offset {!r}".format(raw_offset)) from error
        if offset in result:
            raise ValueError("duplicate flash offset 0x{:x}".format(offset))
        result[offset] = raw_name
    return result


def verify_merged(
    merged: bytes, build_dir: Path, flash_files: Dict[int, str], spec: BoardSpec
) -> None:
    if len(merged) > spec.flash_size:
        raise ValueError("merged image exceeds {} flash".format(spec.flash_size_text))
    if spec.protected and len(merged) > CARDID_OFFSET:
        raise ValueError(
            "merged image reaches protected cardid; even 0xff bytes could erase it"
        )
    for offset, relative_name in flash_files.items():
        image_path = build_dir / relative_name
        image = image_path.read_bytes()
        if merged[offset : offset + len(image)] != image:
            raise ValueError(
                "{} differs at merged offset 0x{:x}".format(
                    relative_name, offset
                )
            )

def flash_plan(spec: BoardSpec, flash_files: Dict[int, str]) -> dict:
    return {
        "schemaVersion": 1,
        "board": spec.name,
        "chip": spec.chip,
        "flashSize": spec.flash_size_text,
        "warning": "Never use erase-flash. Verify the exact board and port before writing.",
        "writeFlashArgs": [
            "--flash_mode",
            "dio",
            "--flash_freq",
            "80m",
            "--flash_size",
            spec.flash_size_text,
        ],
        "segments": [
            {"offset": "0x{:x}".format(offset), "file": name}
            for offset, name in sorted(flash_files.items())
        ],
        "protectedRegions": (
            [
                {
                    "name": "cardid",
                    "offset": "0x356000",
                    "size": "0x4000",
                    "policy": "must-not-write",
                },
                {
                    "name": "permanent-recovery",
                    "offset": "0x700000",
                    "size": "0x100000",
                    "policy": "must-not-write",
                },
            ]
            if spec.protected
            else []
        ),
    }


def verify_flash_plan(path: Path, expected: dict) -> None:
    actual = _read_json(path)
    if actual != expected:
        raise ValueError("{} does not match the verified firmware layout".format(path))


def verify_sdkconfig(path: Path, spec: BoardSpec) -> None:
    values: Dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    expected = {
        "CONFIG_IDF_TARGET": '"{}"'.format(spec.chip),
        "CONFIG_ESPTOOLPY_FLASHSIZE": '"{}"'.format(spec.flash_size_text),
        "CONFIG_APP_REPRODUCIBLE_BUILD": "y",
    }
    for key, value in expected.items():
        if values.get(key) != value:
            raise ValueError("sdkconfig must set {}={}".format(key, value))
    if "CONFIG_BT_ENABLED" in values:
        raise ValueError("sdkconfig must keep Bluetooth disabled")
    if spec.name == "ZECTRIX NOTE4 BLACK-WHITE":
        if values.get("CONFIG_SPIRAM") != "y" or \
                values.get("CONFIG_SPIRAM_MODE_OCT") != "y":
            raise ValueError("NOTE4 sdkconfig must enable octal PSRAM")
    elif "CONFIG_SPIRAM" in values:
        raise ValueError("Passport sdkconfig must not enable PSRAM")


def verify_build(
    board: str,
    build_dir: Path,
    require_merged: bool = False,
    plan_path: Optional[Path] = None,
) -> None:
    spec = SPECS[board]
    sdkconfig_path = build_dir / "sdkconfig"
    if sdkconfig_path.is_file():
        verify_sdkconfig(sdkconfig_path, spec)
    metadata = _read_json(build_dir / "flasher_args.json")
    settings = metadata.get("flash_settings")
    extra = metadata.get("extra_esptool_args")
    if not isinstance(settings, dict) or settings.get("flash_size") != spec.flash_size_text:
        raise ValueError("flasher_args.json selects the wrong flash size")
    if not isinstance(extra, dict) or extra.get("chip") != spec.chip:
        raise ValueError("flasher_args.json selects the wrong chip")

    files = _flash_files(metadata)
    expected = {
        0x0: "bootloader/bootloader.bin",
        PARTITION_TABLE_OFFSET: "partition_table/partition-table.bin",
        FACTORY_OFFSET: spec.app_name,
    }
    if files != expected:
        raise ValueError("unexpected flash segment map: {}".format(files))
    for relative_name in files.values():
        if not (build_dir / relative_name).is_file():
            raise ValueError("missing build image {}".format(relative_name))

    raw_table = (build_dir / expected[PARTITION_TABLE_OFFSET]).read_bytes()
    partitions, found_md5 = parse_partition_table(raw_table)
    verify_partition_contract(partitions, found_md5, spec)

    app_path = build_dir / spec.app_name
    app_size = app_path.stat().st_size
    if app_size > FACTORY_SIZE:
        raise ValueError(
            "application is {} bytes; factory limit is {}".format(
                app_size, FACTORY_SIZE
            )
        )
    if app_path.read_bytes()[:1] != b"\xe9":
        raise ValueError("application image has no ESP image magic")

    bootloader = (build_dir / expected[0]).read_bytes()
    if bootloader[:1] != b"\xe9":
        raise ValueError("bootloader has no ESP image magic")
    if spec.protected and RECOVERY_BOOT_MARKER not in bootloader:
        raise ValueError("bootloader is missing the five-second UP Recovery hook")

    merged_path = build_dir / spec.merged_name
    if require_merged and not merged_path.is_file():
        raise ValueError("missing merged image {}".format(merged_path))
    if merged_path.is_file():
        verify_merged(merged_path.read_bytes(), build_dir, files, spec)

    expected_plan = flash_plan(spec, files)
    if plan_path is not None:
        plan_path.parent.mkdir(parents=True, exist_ok=True)
        plan_path.write_text(
            json.dumps(expected_plan, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    existing_plan = build_dir / "flash-plan.json"
    if existing_plan.is_file():
        verify_flash_plan(existing_plan, expected_plan)

    merged_state = " + merged" if merged_path.is_file() else ""
    print(
        "Firmware layout: PASS ({} / {} bytes{}; {})".format(
            app_size, FACTORY_SIZE, merged_state, spec.name
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", choices=sorted(SPECS))
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--require-merged", action="store_true")
    parser.add_argument("--plan", type=Path)
    args = parser.parse_args()
    try:
        verify_build(
            args.board,
            args.build_dir.resolve(),
            require_merged=args.require_merged,
            plan_path=args.plan,
        )
    except (OSError, UnicodeDecodeError, ValueError) as error:
        print("ERROR: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
