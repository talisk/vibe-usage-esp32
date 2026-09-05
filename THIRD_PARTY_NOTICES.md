# Third-party notices

`vibe-usage-esp32` contains or links the following third-party work. The
project license does not replace the license of any dependency.

## Vendored source

| Work | Pinned source | Files used | License |
| --- | --- | --- | --- |
| FoloToy AI Passport | `FoloToy/ai-passport@f913af29a387f8983ef4faa9f1580b14e88b0740` | Passport BSP and Recovery-compatible bootloader contract | MIT; see `licenses/FoloToy-ai-passport-LICENSE` |
| ZECTRIX NOTE4 E-Paper Reference Demo | `itopinion/zectrix-note4-epd-demo@ca285c98ed0641f86780edb1f5ec77b0335fe649` | NOTE4 board, canvas, and SSD2683 drivers; optional audio/NFC source retained but excluded from P0 builds | MIT; see `licenses/ZECTRIX-note4-demo-LICENSE` |
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
- `espressif/qrcode` 0.2.0 — Apache-2.0.
- LVGL 9.5.0 — MIT.

Component archives include their own license files after dependency
resolution. Release packages also contain a generated SPDX 2.3 inventory.
