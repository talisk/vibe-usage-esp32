#!/usr/bin/env python3
"""Host tests for the firmware partition and protected-region verifier."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "verify_firmware", ROOT / "tools" / "verify_firmware.py"
)
assert SPEC and SPEC.loader
VERIFY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VERIFY
SPEC.loader.exec_module(VERIFY)


def sample_table(passport: bool = True) -> bytes:
    entries = [
        (1, 2, 0x9000, 0x6000, "nvs"),
        (1, 1, 0xF000, 0x1000, "phy_init"),
        (0, 0, 0x10000, 0x300000, "factory"),
    ]
    if passport:
        entries.extend(
            [
                (1, 2, 0x356000, 0x4000, "cardid"),
                (0, 0x20, 0x700000, 0x100000, "recovery"),
            ]
        )
    raw = bytearray(b"\xff" * VERIFY.PARTITION_TABLE_SIZE)
    for index, (kind, subtype, offset, size, label) in enumerate(entries):
        VERIFY.ENTRY.pack_into(
            raw,
            index * VERIFY.ENTRY.size,
            0x50AA,
            kind,
            subtype,
            offset,
            size,
            label.encode().ljust(16, b"\0"),
            0,
        )
    marker = len(entries) * VERIFY.ENTRY.size
    struct.pack_into("<H", raw, marker, 0xEBEB)
    raw[marker + 16 : marker + 32] = hashlib.md5(raw[:marker]).digest()
    return bytes(raw)


class PartitionParserTest(unittest.TestCase):
    def test_passport_contract(self) -> None:
        partitions, found_md5 = VERIFY.parse_partition_table(sample_table())
        VERIFY.verify_partition_contract(
            partitions, found_md5, VERIFY.SPECS["passport"]
        )

    def test_note4_contract(self) -> None:
        partitions, found_md5 = VERIFY.parse_partition_table(
            sample_table(passport=False)
        )
        VERIFY.verify_partition_contract(
            partitions, found_md5, VERIFY.SPECS["note4"]
        )

    def test_rejects_bad_md5(self) -> None:
        raw = bytearray(sample_table())
        raw[28] ^= 1
        with self.assertRaisesRegex(ValueError, "MD5"):
            VERIFY.parse_partition_table(bytes(raw))

    def test_note4_rejects_passport_only_partitions(self) -> None:
        partitions, found_md5 = VERIFY.parse_partition_table(sample_table())
        with self.assertRaisesRegex(ValueError, "unexpected"):
            VERIFY.verify_partition_contract(
                partitions, found_md5, VERIFY.SPECS["note4"]
            )

    def test_passport_merged_rejects_identity_payload(self) -> None:
        spec = VERIFY.SPECS["passport"]
        merged = bytearray(b"\xff" * (VERIFY.CARDID_OFFSET + 1))
        merged[VERIFY.CARDID_OFFSET] = 0
        with self.assertRaisesRegex(ValueError, "cardid"):
            VERIFY.verify_merged(bytes(merged), ROOT, {}, spec)

    def test_passport_merged_rejects_ff_over_protected_region(self) -> None:
        spec = VERIFY.SPECS["passport"]
        merged = b"\xff" * (VERIFY.CARDID_OFFSET + 1)
        with self.assertRaisesRegex(ValueError, "could erase"):
            VERIFY.verify_merged(merged, ROOT, {}, spec)

    def test_flash_plan_round_trip(self) -> None:
        expected = VERIFY.flash_plan(
            VERIFY.SPECS["note4"],
            {
                0: "bootloader/bootloader.bin",
                VERIFY.PARTITION_TABLE_OFFSET:
                    "partition_table/partition-table.bin",
                VERIFY.FACTORY_OFFSET: "vibe_usage_zectrix.bin",
            },
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "flash-plan.json"
            path.write_text(json.dumps(expected), encoding="utf-8")
            VERIFY.verify_flash_plan(path, expected)

    def test_flash_plan_rejects_changed_segment(self) -> None:
        expected = VERIFY.flash_plan(
            VERIFY.SPECS["passport"],
            {0: "bootloader/bootloader.bin"},
        )
        changed = dict(expected)
        changed["segments"] = [{"offset": "0x0", "file": "wrong.bin"}]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "flash-plan.json"
            path.write_text(json.dumps(changed), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "verified firmware layout"):
                VERIFY.verify_flash_plan(path, expected)

    def test_sdkconfig_contract(self) -> None:
        content = "\n".join(
            (
                'CONFIG_IDF_TARGET="esp32s3"',
                'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
                "CONFIG_APP_REPRODUCIBLE_BUILD=y",
                "# CONFIG_BT_ENABLED is not set",
                "CONFIG_SPIRAM=y",
                "CONFIG_SPIRAM_MODE_OCT=y",
            )
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sdkconfig"
            path.write_text(content, encoding="utf-8")
            VERIFY.verify_sdkconfig(path, VERIFY.SPECS["note4"])

    def test_sdkconfig_rejects_bluetooth(self) -> None:
        content = "\n".join(
            (
                'CONFIG_IDF_TARGET="esp32c3"',
                'CONFIG_ESPTOOLPY_FLASHSIZE="8MB"',
                "CONFIG_APP_REPRODUCIBLE_BUILD=y",
                "CONFIG_BT_ENABLED=y",
            )
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sdkconfig"
            path.write_text(content, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Bluetooth disabled"):
                VERIFY.verify_sdkconfig(path, VERIFY.SPECS["passport"])


if __name__ == "__main__":
    unittest.main()
