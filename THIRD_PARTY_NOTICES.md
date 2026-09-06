# Third-party notices

`vibe-usage-esp32` contains or links the following third-party work. The
project license does not replace the license of any dependency.

## Vendored source

| Work | Pinned source | Files used | License |
| --- | --- | --- | --- |
| FoloToy AI Passport | `FoloToy/ai-passport@f913af29a387f8983ef4faa9f1580b14e88b0740` | Passport BSP and Recovery-compatible bootloader contract | MIT; see `licenses/FoloToy-ai-passport-LICENSE` |
| ZECTRIX NOTE4 E-Paper Reference Demo | `itopinion/zectrix-note4-epd-demo@ca285c98ed0641f86780edb1f5ec77b0335fe649` | NOTE4 board, canvas, SSD2683 drivers and hardware mapping for product audio/NFC adapters; original demo audio/NFC classes retained but not linked | MIT; see `licenses/ZECTRIX-note4-demo-LICENSE` |
| esp-wifi-connect | `78/esp-wifi-connect@347682fa013b52f863052ad4b1a793ca3cabff17` | Captive portal, DNS, station selection, and credential store, with documented local hardening | MIT; see `licenses/esp-wifi-connect-LICENSE` |

Local modifications to vendored source are described in
[`docs/upstream-lock.md`](docs/upstream-lock.md). Copyright notices in those
files remain with their original authors.

## Managed build dependencies

The generated Latin/CJK subsets in `components/vibe_ui/generated/vibe_font_*.c` and
`vibe_glyphs.c` derive from Noto Sans CJK JP Regular (Sans2.004), copyright
2014–2021 Adobe, under SIL Open Font License 1.1. The derivatives are named
`vibe_font_*` / `vibe_glyphs`; they remain OFL-licensed, not MIT. See
`licenses/Noto-CJK-OFL-1.1.txt` and `docs/upstream-lock.md` for the pinned font,
checksum, and regeneration. No online font download occurs on a device.

ESP-IDF Component Manager resolves exact versions and hashes from
`firmware/*/dependencies.lock`:

- Espressif ESP-IDF 5.5.3 — Apache-2.0.
- `espressif/button` 4.2.0 — Apache-2.0.
- `espressif/cmake_utilities` 1.1.1 — Apache-2.0.
- `espressif/esp_lvgl_port` 2.9.0 — Apache-2.0.
- `espressif/esp_codec_dev` 1.6.2 — Apache-2.0; ES8311 control and I2S format
  integration on both boards. Exact archive hash is recorded in both lock files
  and `docs/upstream-lock.md`.
- `espressif/qrcode` 0.2.0 — Apache-2.0.
- LVGL 9.5.0 — MIT.

Component archives include their own license files after dependency
resolution. Release packages also contain a generated SPDX 2.3 inventory.

## cJSON host test copy

`tests/host/vendor/cjson/` contains unmodified cJSON 1.7.19 `cJSON.c`,
`cJSON.h` and `LICENSE`, copied from the ESP-IDF 5.5.3 `components/json/cJSON`
submodule. It is MIT-licensed by Dave Gamble and contributors. This copy makes
host protocol/storage tests reproducible without an installed ESP-IDF; firmware
continues to use ESP-IDF's own `json` component.
Upstream: https://github.com/DaveGamble/cJSON/tree/v1.7.19

## Optional host subscription gateway

`services/codex_gateway/requirements.txt` installs these Python packages on the
host, not in either firmware image. License identifiers below were checked in
the installed package metadata; wheels include their own license notices and
may contain separately licensed native dependencies such as FFmpeg.

- aiohttp 3.13.3 — Apache-2.0 AND MIT.
- aiortc 1.15.0 — BSD-3-Clause.
- PyAV (`av`) 17.1.0 — BSD-3-Clause.

The official Codex CLI 0.153.3 is a separately installed runtime dependency;
its binaries and account credentials are not bundled in this repository.
