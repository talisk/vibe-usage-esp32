#!/usr/bin/env python3
"""Opt-in read-only API versus firmware-core check; personal data stays in RAM."""
import argparse
import ctypes
import datetime as dt
import json
from pathlib import Path
import subprocess
import tempfile
import urllib.parse
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--config", type=Path, required=True,
                      help="Existing local Vibe CLI config; never copied to firmware")
    options = args.parse_args()
    config = json.loads(options.config.read_text())
    if config.get("apiUrl", "").rstrip("/") != "https://vibecafe.ai":
        raise ValueError("Config does not use the trusted service")
    key = config.get("apiKey")
    if not isinstance(key, str) or not key.startswith("vbu_"):
        raise ValueError("Config has no Vibe credential")
    root = Path(__file__).resolve().parents[1]
    core = root / "components/vibe_usage"
    opener = urllib.request.build_opener(NoRedirect)
    zone = dt.timezone(dt.timedelta(hours=8))
    today = dt.datetime.now(zone).date()
    with tempfile.TemporaryDirectory(prefix="vibe-live-core-") as temp:
        libpath = Path(temp) / "probe.so"
        sources = [core / "src" / (name + ".c") for name in
                   ("vibe_usage", "vibe_date", "vibe_aggregate", "vibe_cache", "vibe_parser")]
        subprocess.run(["cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-I" + str(core / "include"),
                        str(root / "tests/host/live_probe.c"),
                        *map(str, sources), "-o", str(libpath)], check=True)
        lib = ctypes.CDLL(str(libpath))
        lib.vibe_probe_create.restype = ctypes.c_void_p
        lib.vibe_probe_destroy.argtypes = [ctypes.c_void_p]
        lib.vibe_probe_day.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                      ctypes.c_size_t, ctypes.c_int32,
                                      ctypes.POINTER(ctypes.c_uint64)]
        lib.vibe_date_api_bounds.argtypes = [ctypes.c_int32, ctypes.c_int,
                                             ctypes.c_char_p, ctypes.c_char_p]
        lib.vibe_probe_snapshot.argtypes = [ctypes.c_void_p, ctypes.c_int32,
                                           ctypes.POINTER(ctypes.c_uint64),
                                           ctypes.POINTER(ctypes.c_uint64),
                                           ctypes.POINTER(ctypes.c_uint8)]
        context = lib.vibe_probe_create()
        if not context:
            raise MemoryError()
        totals = []
        try:
            for age in range(7):
                day = today - dt.timedelta(days=age)
                day_key = int(day.strftime("%Y%m%d"))
                start, end = ctypes.create_string_buffer(25), ctypes.create_string_buffer(25)
                if lib.vibe_date_api_bounds(day_key, 0, start, end):
                    raise ValueError("Core date bounds failed")
                query = urllib.parse.urlencode({"from": start.value.decode(),
                                                "to": end.value.decode(),
                                                "tz": "Asia/Shanghai"})
                request = urllib.request.Request("https://vibecafe.ai/api/usage?" + query,
                    headers={"Authorization": "Bearer " + key, "Accept": "application/json"})
                with opener.open(request, timeout=30) as response:
                    if response.status != 200:
                        raise ValueError("Usage HTTP status was not 200")
                    body = response.read(2 * 1024 * 1024 + 1)
                if len(body) > 2 * 1024 * 1024:
                    raise ValueError("Response exceeds firmware limit")
                data = json.loads(body)
                expected = 0
                for bucket in data["buckets"]:
                    stamp = dt.datetime.fromisoformat(bucket["bucketStart"].replace("Z", "+00:00"))
                    if stamp.astimezone(zone).date() != day:
                        raise ValueError("Response contains an out-of-day bucket")
                    value = bucket["totalTokens"]
                    if type(value) is not int or not 0 <= value <= 2**64 - 1:
                        raise ValueError("Invalid bucket integer")
                    expected += value
                actual = ctypes.c_uint64()
                result = lib.vibe_probe_day(context, body, len(body), day_key,
                                           ctypes.byref(actual))
                if result or actual.value != expected:
                    raise ValueError("Firmware parser and independent integer sum differ")
                totals.append(expected)
                print("day_offset=-{} HTTP=200 bounds=PASS parser_sum=PASS".format(age), flush=True)
            one, week, mask = ctypes.c_uint64(), ctypes.c_uint64(), ctypes.c_uint8()
            result = lib.vibe_probe_snapshot(context, int(today.strftime("%Y%m%d")),
                ctypes.byref(one), ctypes.byref(week), ctypes.byref(mask))
            if result or one.value != totals[0] or week.value != sum(totals) or mask.value != 127:
                raise ValueError("Firmware snapshot and independent sums differ")
            print("Live API / host firmware core: PASS (Today, 7D, coverage=7/7; no totals retained)")
        finally:
            lib.vibe_probe_destroy(context)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Exception strings can include request URLs; emit only their type.
        print("Live probe: FAIL ({})".format(type(error).__name__))
        raise SystemExit(1)
