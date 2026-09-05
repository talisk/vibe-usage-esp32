# Shared font baseline and About QR — 0.0.1 / 2026-09-05

Current candidate supersedes [i18n/portal](2026-09-05-i18n-portal.md), without
rewriting historical evidence. Initial uncommitted workspace on unborn `main`;
not a published revision. Root `VERSION` remains 0.0.1.

## Changes and diagnosis

The owner's photos IMG_9444/IMG_9445 show inconsistent Chinese placement and
weight. The old generator centered each glyph's ink bounds into a square,
clipped some extents, and used separate Latin fallback fonts. Note also used a
38% coverage threshold, making antialiased CJK strokes expand in monochrome.

- Latin and CJK now share the same locked OFL Noto Sans face, origin and
  alphabetic baseline. No individual glyph centering, resizing or cropping.
  A padded render asserts every source glyph fits without losing ink.
- Note: 16px em, 20px cell height, baseline 15, variable native advances,
  50% monochrome coverage. Passport: 4-bpp, logical 12/14/20 profiles with
  em 12/13/18 and line heights 16/16/22; Latin has no mixed-font fallback.
  Passport's large numeric totals retain their existing Montserrat face.
- Actual new-font width checks caught the Passport 100.0% column and Japanese
  Timezone label: column widths now leave sufficient room without overlapping
  adjacent values. A Japanese Note password hint was shortened to fit 146px.
- About UP opens Repository, DOWN opens X; from either QR, UP/DOWN switches
  target and OK returns to About. OK from About returns to Settings.
  Titles and `Scan to visit.` are localized to all four supported languages.
- QR targets are exactly `https://github.com/talisk/vibe-usage-esp32` and
  `https://x.com/SwainTalisk`. Integer modules and four-module white quiet zones
  are retained. Note treats QR layout/target changes as full-refresh events.

Wi-Fi, authorization, cache and saved language are not erased or migrated.
No protected identity/Recovery access, eFuse changes, service upload or repository
publication. The obsolete legacy-ASCII metric test was replaced by checks
against the real current Note canvas; the upstream font header remains intact.

## Host and rendering evidence

- Core sanitizer tests; catalog/default/cycle/UTF-8 coverage; actual config
  migration and failure paths; exhaustive Note glyph and inverse selection tests.
- Actual LVGL checks all generated glyphs resolve to the same face with the
  shared baseline offset, fixed label heights, all-language compact fields,
  product links, rounded black corners and both footer arrow pixel regions.
- Real Note canvas and real managed QR encoder test both target URLs, integer
  scaling and white quiet zones. Shared About navigation tests cover all views.
- Actual rendered font samples were inspected locally at 3x nearest-neighbor:
  `立即刷新`, `VibeCafe / 已关联`, `时区 / Asia/Shanghai`, traditional Chinese,
  Japanese and Latin samples. This is host raster evidence, not panel acceptance.
- Apple Vision independently decoded all four actual rendered QR PNGs to the
  exact expected URLs. This is not phone-camera scanning of physical panels.

Reproduce optional render artifacts (ignored `build/`, no personal data):

```bash
mkdir -p build/ui-previews
VIBE_UI_PREVIEW_DIR="$PWD/build/ui-previews" ./tools/test-host.sh
VIBE_UI_PREVIEW_DIR="$PWD/build/ui-previews" ./tools/test-ui-layout.sh
```

The tests emit PGM/PPM images. Convert them to PNG with Pillow, then on macOS run
`swift tests/host/decode_about_qr.swift build/ui-previews/*-qr.png`.
The standard validation gate does not require Pillow, Swift or Apple Vision.

## Build and device evidence

ESP-IDF 5.5.3 full `tools/validate.sh` passed, including local actionlint, both
clean builds, stack budgets and protected-layout verification. A final static
gate also passed after adding QR caption-width checks. Clean merged images are
byte-identical to the incremental candidates flashed below.

| Product | App bytes | Application SHA-256 |
| --- | --- | --- |
| Vibe Passport | 2061328 | `5b000c35ea0764d2bf9f4c95b14f5b613af3a7414b827b1d76fb50f032971f45` |
| Vibe Note | 1372384 | `be4f727bfd062948723785de801fec01936dd6981664ef44d5fae2bd7e276939` |

Merged image SHA-256:

- Passport: `a5571e360e823a7f69b0f9f2e7d3ddb3b8368380e5c8431d05b478d3264e0fb8`.
- Note: `0afeb13e58319f86a742ad3b2e3f26c6af817a8444dc5d51421082845bc16bab`.

Both devices were USB-powered. Serial ports were freshly rediscovered before
each flash. Installer read-only identity checks confirmed Passport ESP32-C3 /
8 MB on `/dev/cu.usbmodem1101` and Note ESP32-S3 / 16 MB on
`/dev/cu.usbmodem101`. Hardware baseline remains Passport no PSRAM and Note
8 MB octal PSRAM; this run did not perform a separate PSRAM stress test.
Only bootloader 0x0, partition table 0x8000 and app 0x10000 were written;
all three segments passed on-device hash verification on each board.

Opening the bounded, health-only serial observers caused normal reboots.
Both reported version 0.0.1, restored auth/cache, successful TLS certificate
validation and a fresh authenticated HTTP 200 sync. Sanitized observations:

```text
Passport:
auth=1 cache_restored=1 error=OK
state=9 auth=1 http=200 error=OK system=0x0 today_complete=1
coverage=7/7 persisted=1/1
heap_internal=72520 largest_internal=27648 stack_free=4312

Note:
auth=1 cache_restored=1 error=OK
state=9 auth=1 http=200 error=OK system=0x0 today_complete=1
coverage=7/7 persisted=1/1
heap_internal=153307 largest_internal=32768 stack_free=3716
full display refresh visual=0
```

No panic/assert/allocation-failure marker appeared during the bounded 45-second
observations. This is point-in-time boot/sync evidence, not a long-run memory or
physical UI guarantee. No personal usage totals or credentials were recorded.

| Category | Status | Scope |
| --- | --- | --- |
| Build | PASS | Clean dual builds, image equality, size/layout and stack budgets |
| Host tests | PASS | Core/config/catalog, current glyphs/LVGL/canvas, QR navigation/pixels/decode |
| CI | NOT RUN | Local actionlint only; no remote workflow |
| Real API | PASS, scoped | Both post-flash authenticated GET 200, TLS validation, 7/7 |
| Device tests | PARTIAL | Both boot/restored state/sync; Note EPD full refresh; physical UI pending |
| Installer tests | PASS, scoped | Both segmented writes and all segment hash checks |
| Power tests | NOT RUN | USB only; battery/current/brownout/latch tests not repeated |
| Unverified | OPEN | Physical checks below; no whole-device acceptance claim |

## Physical recheck

1. In Simplified Chinese Settings, inspect `立即刷新`, `已关联`, and the mixed
   `时区 / Asia/Shanghai` baseline. Compare Chinese and Latin stroke weight on
   Note at normal viewing distance. Check traditional Chinese and Japanese too.
2. Open About, short-press UP and scan Repository; short-press DOWN and scan X.
   Confirm titles/hints follow the selected language. OK returns to About.
3. On Note, ensure switching QR targets/returning clears the old QR completely.
   Check Passport's quiet zones stay white inside the rounded black exterior.
4. Confirm saved Wi-Fi/account/language remain intact. Do not use Reset Settings
   unless intentionally testing the destructive confirmation flow.

Remote CI, physical typography/button/phone scanning, captive-portal save,
Recovery, battery/current, abrupt power loss and long-run EPD ghosting remain
unverified by these host tests.
