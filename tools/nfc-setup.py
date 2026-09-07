#!/usr/bin/env python3
"""Export a static, open-AP Passport NDEF message. Never writes a device.

The binary is an NDEF message, not a chip/flash dump. The companion JSON lists
the two records for a phone NFC writer. Passport NTAG213 has 144 user bytes.
"""
import argparse
import json
from pathlib import Path
import struct


def attribute(kind: int, value: bytes) -> bytes:
    return struct.pack(">HH", kind, len(value)) + value


def build_ndef(ssid: str, url: str = "http://192.168.4.1/") -> bytes:
    ssid_bytes = ssid.encode("utf-8")
    if not 1 <= len(ssid_bytes) <= 32 or "\0" in ssid:
        raise ValueError("SSID must contain 1..32 UTF-8 bytes and no NUL")
    if not url.startswith(("http://", "https://")) or any(ord(c) <= 32 or ord(c) == 127 for c in url):
        raise ValueError("URL must be an HTTP(S) URL without whitespace")
    url_bytes = url.encode("utf-8")
    if len(url_bytes) > 192:
        raise ValueError("URL exceeds firmware's 192-byte limit")
    credential = b"".join((
        attribute(0x1026, b"\x01"),
        attribute(0x1045, ssid_bytes),
        attribute(0x1003, b"\x00\x01"),  # Open authentication
        attribute(0x100F, b"\x00\x01"),  # No encryption
        attribute(0x1027, b""),           # No password; never accept router keys.
        attribute(0x1020, b"\xff" * 6),
    ))
    payload = attribute(0x104A, b"\x10") + attribute(0x100E, credential)
    mime = b"application/vnd.wfa.wsc"
    wifi = bytes((0x92, len(mime), len(payload))) + mime + payload
    prefix_len, prefix_code = (8, 4) if url.startswith("https://") else (7, 3)
    if len(url_bytes) <= prefix_len or url_bytes[prefix_len] in b"/?":
        raise ValueError("URL must contain a host")
    uri_payload = bytes((prefix_code,)) + url_bytes[prefix_len:]
    uri = bytes((0x51, 1, len(uri_payload))) + b"U" + uri_payload
    return wifi + uri


def type2_tlv(message: bytes) -> bytes:
    if len(message) <= 254:
        return bytes((3, len(message))) + message + b"\xfe"
    return b"\x03\xff" + struct.pack(">H", len(message)) + message + b"\xfe"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssid", required=True, help="Exact open AP SSID displayed on the Passport")
    parser.add_argument("--url", default="http://192.168.4.1/", help="Fixed AP portal URL")
    parser.add_argument("--output", type=Path, required=True, help="Output NDEF message path, e.g. passport.ndef")
    args = parser.parse_args()
    try:
        message = build_ndef(args.ssid, args.url)
        tlv = type2_tlv(message)
        if len(tlv) > 144:
            raise ValueError(f"NDEF needs {len(tlv)} user bytes; Passport NTAG213 allows 144")
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(message)
    args.output.with_suffix(".records.json").write_text(json.dumps({
        "hardware": "Passport NTAG213", "ndef_bytes": len(message),
        "type2_user_bytes": len(tlv), "capacity": 144,
        "records": [
            {"type": "Wi-Fi", "mime": "application/vnd.wfa.wsc", "ssid": args.ssid,
             "authentication": "Open", "encryption": "None", "password": ""},
            {"type": "URI", "url": args.url},
        ],
        "phone_write": "In NFC Tools Write, add Wi-Fi (Open, this SSID), then URL; write both records together. Do not lock the tag.",
        "verification": "Read records back in the phone app. Binary export alone does not mean the physical tag was written.",
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.output} ({len(message)} NDEF bytes; {len(tlv)}/144 tag user bytes)")
    print(f"Phone record fields: {args.output.with_suffix('.records.json')}")
    print("Phone still needs to write the passive NFC tag once; see docs/smart-todo-hardware.md.")


if __name__ == "__main__":
    main()
