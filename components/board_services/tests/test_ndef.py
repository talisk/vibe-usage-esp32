#!/usr/bin/env python3
"""Host test: independently decode real C NDEF output and compare exporter.

No SDK, device, UID, Wi-Fi credentials or personal data is accessed.
"""
import ctypes
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "components/board_services"


def records(data):
    result = []
    cursor = 0
    while cursor < len(data):
        flags, type_size = data[cursor:cursor + 2]
        cursor += 2
        assert flags & 0x10 and not flags & 0x28  # short, not chunked, no ID
        payload_size = data[cursor]
        cursor += 1
        kind = data[cursor:cursor + type_size]
        cursor += type_size
        payload = data[cursor:cursor + payload_size]
        cursor += payload_size
        assert len(payload) == payload_size
        assert bool(flags & 0x80) == (not result)
        assert bool(flags & 0x40) == (cursor == len(data))
        result.append((flags & 7, kind, payload))
    assert cursor == len(data)
    return result


def attrs(data):
    result = {}
    cursor = 0
    while cursor < len(data):
        kind = int.from_bytes(data[cursor:cursor + 2], "big")
        size = int.from_bytes(data[cursor + 2:cursor + 4], "big")
        cursor += 4
        result[kind] = data[cursor:cursor + size]
        assert len(result[kind]) == size
        cursor += size
    assert cursor == len(data)
    return result


class NdefTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="vibe-ndef-host-")
        lib = Path(cls.temp.name) / "ndef.so"
        subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra",
                        "-Werror", "-shared", "-fPIC", str(COMPONENT / "board_ndef.c"),
                        "-o", str(lib)], check=True)
        cls.lib = ctypes.CDLL(str(lib))
        ptr = ctypes.POINTER(ctypes.c_ubyte)
        sizeptr = ctypes.POINTER(ctypes.c_size_t)
        cls.lib.board_ndef_wifi.argtypes = [ctypes.c_char_p] * 3 + [ptr, ctypes.c_size_t, sizeptr]
        cls.lib.board_ndef_wifi.restype = ctypes.c_bool
        cls.lib.board_ndef_uri.argtypes = [ctypes.c_char_p, ptr, ctypes.c_size_t, sizeptr]
        cls.lib.board_ndef_uri.restype = ctypes.c_bool
        cls.lib.board_ndef_type2.argtypes = [ptr, ctypes.c_size_t, ptr, ctypes.c_size_t, sizeptr]
        cls.lib.board_ndef_type2.restype = ctypes.c_bool
        spec = importlib.util.spec_from_file_location("nfc_setup", ROOT / "tools/nfc-setup.py")
        cls.exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.exporter)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def encode(self, ssid=b"VibePassport-TEST", password=b"", url=b"http://192.168.4.1/", capacity=400):
        out = (ctypes.c_ubyte * 512)(*([0xA5] * 512))
        length = ctypes.c_size_t(999)
        ok = self.lib.board_ndef_wifi(ssid, password, url, out, capacity, ctypes.byref(length))
        self.assertEqual(bytes(out[capacity:]), b"\xA5" * (512 - capacity))
        return ok, bytes(out[:length.value]), length.value

    def test_open_ap_credentials_and_url(self):
        ok, data, _ = self.encode()
        self.assertTrue(ok)
        first, second = records(data)
        self.assertEqual(first[:2], (2, b"application/vnd.wfa.wsc"))
        wsc = attrs(first[2])
        self.assertEqual(wsc[0x104A], b"\x10")
        credential = attrs(wsc[0x100E])
        self.assertEqual(credential[0x1045], b"VibePassport-TEST")
        self.assertEqual(credential[0x1003], b"\x00\x01")
        self.assertEqual(credential[0x100F], b"\x00\x01")
        self.assertEqual(credential[0x1027], b"")
        self.assertEqual(second, (1, b"U", b"\x03192.168.4.1/"))

    def test_utf8_and_full_length_protected_network(self):
        for ssid in [b"X" * 32, "测试热点".encode()]:
            ok, data, _ = self.encode(ssid, b"T" * 63)
            self.assertTrue(ok)
            credential = attrs(attrs(records(data)[0][2])[0x100E])
            self.assertEqual(credential[0x1045], ssid)
            self.assertEqual(credential[0x1003], b"\x00\x20")
            self.assertEqual(credential[0x100F], b"\x00\x08")
            self.assertEqual(credential[0x1027], b"T" * 63)

    def test_reject_invalid_and_truncated_inputs(self):
        for args in [(b"", b""), (b"x" * 33, b""), (b"x", b"short"), (b"x", b"k" * 64)]:
            self.assertEqual(self.encode(*args)[::2], (False, 0))
        for url in [b"javascript:alert(1)", b"http://bad\n", b"https://" + b"x" * 190]:
            self.assertFalse(self.encode(url=url)[0])
        ok, _, length = self.encode()
        self.assertTrue(ok)
        for capacity in range(length):
            ok, _, actual = self.encode(capacity=capacity)
            self.assertFalse(ok)
            self.assertEqual(actual, 0)

    def test_single_uri_record(self):
        out = (ctypes.c_ubyte * 200)()
        length = ctypes.c_size_t()
        self.assertTrue(self.lib.board_ndef_uri(b"https://example.test/", out, len(out), ctypes.byref(length)))
        self.assertEqual(records(bytes(out[:length.value])), [(1, b"U", b"\x04example.test/")])

    def test_type2_short_long_boundaries(self):
        for length in [1, 254, 255, 300]:
            message = b"x" * length
            src = (ctypes.c_ubyte * length).from_buffer_copy(message)
            out = (ctypes.c_ubyte * 512)()
            actual = ctypes.c_size_t()
            self.assertTrue(self.lib.board_ndef_type2(src, length, out, 512, ctypes.byref(actual)))
            self.assertEqual(bytes(out[:actual.value]), self.exporter.type2_tlv(message))
            self.assertFalse(self.lib.board_ndef_type2(src, length, out, actual.value - 1, ctypes.byref(actual)))
            self.assertEqual(actual.value, 0)

    def test_passport_export_matches_firmware_and_fits_tag(self):
        for ssid in ["VibePassport-TEST", "x" * 32, "测试热点"]:
            expected = self.exporter.build_ndef(ssid)
            ok, data, _ = self.encode(ssid.encode())
            self.assertTrue(ok)
            self.assertEqual(data, expected)
            self.assertLessEqual(len(self.exporter.type2_tlv(data)), 144)


if __name__ == "__main__":
    unittest.main()
