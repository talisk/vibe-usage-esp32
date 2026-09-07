# Upstream source lock and local changes

The repository vendors hardware and Wi-Fi code so a release does not depend on
the moving head of another project. The commit, copied paths, license, and local
delta must be reviewed together whenever a snapshot changes.

## Locked sources

| Upstream | Commit | Local destination | License |
| --- | --- | --- | --- |
| `FoloToy/ai-passport` | `f913af29a387f8983ef4faa9f1580b14e88b0740` | `firmware/ai-passport/components/bsp`, `firmware/ai-passport/bootloader_components/recovery_boot_hook` | MIT |
| `itopinion/zectrix-note4-epd-demo` | `ca285c98ed0641f86780edb1f5ec77b0335fe649` | `firmware/zectrix/components/zectrix_board`, `zectrix_epd`, `zectrix_canvas` | MIT |
| `78/esp-wifi-connect` | `347682fa013b52f863052ad4b1a793ca3cabff17` | `components/esp_wifi_connect` | MIT |

The corresponding license text is under `licenses/`. Managed component
versions and archive hashes are independently frozen in each
`firmware/*/dependencies.lock`.

## AI Passport delta

Most BSP and Recovery-hook files are byte-for-byte copies. Product-owned
changes are limited to:

- `bsp_button`: expose current button level through the already-owned ADC
  button device, use a 1500 ms long-press threshold, and forward
  `BUTTON_PRESS_UP` as `BSP_BTN_RELEASE` for non-blocking voice-stop signaling;
- product `sdkconfig.defaults`: fixed 8 MB layout, USB Serial/JTAG console,
  LVGL QR/fonts and bounded memory, no PSRAM, unused Bluetooth disabled;
- upstream `bsp_audio.c` remains excluded from the BSP build; the product now
  compiles `components/board_services/board_audio.cc` and links codec/I2S for
  TODO microphone capture and reminder sounds. It preserves the upstream
  ES8311 pin map, shared I2C ownership, `no_dac_ref=true`, 30 dB input gain, and
  close/reopen channel workaround, with bounded 8 kHz mono capture;
- Passport's independent passive NTAG213 has no MCU interface. Its firmware
  NFC write API returns `ESP_ERR_NOT_SUPPORTED`; `tools/nfc-setup.py` exports
  static open-AP WSC + URI records for one-time writing with a phone;
- product partition CSV: preserve the upstream 24 KiB NVS, factory app,
  cardid, and permanent Recovery offsets exactly;
- product app/UI: replace the demonstration application, without distributing
  factory identity or the Recovery image.

The bootloader hook is preserved as source, not reimplemented from a guessed
partition. It samples UP for five seconds and boots the existing permanent
Recovery partition. `tools/verify_firmware.py` checks its marker in every built
Passport bootloader.

## ZECTRIX NOTE4 delta

The SSD2683 driver, waveform, RTC, board drivers, optional audio/NFC source,
and ASCII font come from the locked demo. Product-owned changes are:

- board buttons emit click on release for all three keys, avoiding a click
  before an UP/DOWN long-hold action, and expose a read-only pressed query.
  A long hold emits `kRelease` on release, and long OK release also calls a
  non-blocking callback directly from the button task so EPD rendering cannot
  delay the controller's voice-stop flag;
- the reusable canvas is split out of the demo UI; menu/demo business code is
  not carried into the product;
- the canvas decodes UTF-8, measures/draws the product Latin/CJK subset at 16px em,
  preserves inverse selection rendering, and bounds centered copy to two lines;
- the product UI owns frame comparison, full/partial policy, QR rendering,
  settings, and controller integration;
- the original demo audio and NFC classes remain excluded. Product-owned
  `board_services/board_audio.cc` and `board_nfc_note.cc` now compile: they reuse
  the board's I2C handle/lock and audio/NFC pin map, without UID reads or logs.
  NFC remains powered down outside configuration updates; stop rewrites and
  verifies a safe URI before lowering the rail, since RF can read EEPROM with
  the MCU supply off;
- GPIO17 is asserted by the first statement in `app_main`, before NVS and all
  peripheral initialization, and is retained for the deep-sleep fallback;
- the product config fixes ESP32-S3, 16 MB flash, and 8 MB octal PSRAM.

The board pin map remains the source of truth: OK GPIO0, RTC interrupt GPIO5,
power latch GPIO17, DOWN GPIO18, and UP GPIO39. NOTE4C is outside this snapshot.

## Smart TODO audio and NFC dependency provenance

The new product component manifest at `components/board_services/idf_component.yml`
pins `espressif/esp_codec_dev` exactly to **1.6.2**. This matches the selected
Passport reference BSP dependency; NOTE4's old demo used a broader `~1.5`
range, which is not reused for the product. The managed codec is not patched.

Both `firmware/ai-passport/dependencies.lock` and
`firmware/zectrix/dependencies.lock` resolve the official
`https://components.espressif.com/` service to version `1.6.2` with component hash:

```text
4779f31a3ba3f9b38aee1afe6ccb9ba8f6108dc3a6e77d39d7d714e9a215371b
```

The resolved archive's `.component_hash` matches both locks, and its `LICENSE`
is Apache-2.0. This is a Component Manager archive-content hash, not a claimed
upstream Git commit or firmware image SHA256. Keep the two lock files in sync
when intentionally changing the manifest version, and repeat both target builds.

`components/board_services/board_ndef.c` is a product-owned bounded encoder for
WSC credentials, NFC Forum URI records and Type 2 NDEF TLVs. The NOTE4 adapter
uses the locked demo's block size, `0x01..0x37` user range, and delayed
STOP/read transaction shape, but adds field-arbitration failures, read-before-
write, full stale-data clearing and readback verification. It never accesses
the demo's UID/CC block, config, lock, or identity storage. The exact NOTE4 NFC
chip order code is not asserted from its I2C address alone.

The hardware implementation, official source links, Passport phone-write
instructions, compatibility limits, and independent NDEF tests are documented
in [smart-todo-hardware.md](smart-todo-hardware.md). Source and build checks do
not establish microphone quality, reminder audibility, or phone WSC support.

## esp-wifi-connect delta

The component is vendored rather than edited under `managed_components/`. Its
public behavior remains an open captive portal at `192.168.4.1`, a station
manager, and up to ten saved 2.4 GHz networks. Local hardening includes:

- explicit-length copies and strict request/body/JSON/credential bounds;
- unique typed advanced-config fields, validated power values, commit-before-
  publish behavior, and rollback of driver power on NVS failure;
- error propagation for set-default, delete, and store operations;
- no password or Authorization logging;
- removal of the upstream README's whole-NVS erase fallback example;
- station/config timers created before Wi-Fi or scan callbacks can fire;
- saved-network priority preserved while selecting the strongest visible BSSID;
- BSSID pinning only when scan data is valid;
- an active hidden-SSID fallback even when the SSID is absent from scan results;
- queue/timer cleanup and product-facing state callbacks through `wifi_adapter`.
- public-only product metadata at `/project-info`, an embedded `/project-info.js`
  About block on setup/success pages, localized copy, and scrollable success
  layout; no new credential fields or external runtime dependencies;
- the next AP session takes its default language from the saved product setting.

These changes affect security and reconnect semantics; an upstream bump must not
replace them silently.

## Update procedure

### CJK font source and regeneration

Font source: [Noto CJK](https://github.com/notofonts/noto-cjk/tree/523d033d6cb47f4a80c58a35753646f5c3608a78),
Sans2.004, commit `523d033d6cb47f4a80c58a35753646f5c3608a78`,
`Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf`.
Input SHA256: `68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5`.
License: SIL OFL 1.1 (`licenses/Noto-CJK-OFL-1.1.txt`), copyright Adobe 2014–2021.
Generated subsets use renamed `vibe_font_*` and `vibe_glyphs` symbols and retain OFL.

Use Python with Pillow 11.3.0 and fontTools, then run
`python3 tools/generate_ui_resources.py /path/to/NotoSansCJKjp-Regular.otf`.
The generator reads all four columns of `components/vibe_ui/messages.tsv` and
emits shared ASCII + CJK subsets with a fixed alphabetic origin (not per-glyph
ink-box centering). Note uses a 16px em, 20px cell height, baseline 15, native
advances and 50% 1-bpp coverage. Passport logical sizes 12/14/20 use em sizes
12/13/18, line heights 16/16/22 and baselines 12/12/17 in 4-bpp. The generator
asserts that padded glyph ink is never cropped. Latin no longer falls back to
Montserrat or the legacy Note ASCII face in mixed text; large Passport totals
retain Montserrat. The upstream ASCII header is retained only for provenance.
Generated C is checked in; normal builds require neither Python font packages
nor an online font download. Rerun the generator when adding translated copy,
then run the full validation gate. Only ASCII, bundled UI copy and language autonyms
are covered; this is not a general Unicode font.

1. Fetch the candidate upstream revision without changing the locked snapshot.
2. Read its license, release notes, hardware config, partitions, and dependency
   changes.
3. Diff every copied file against both the old and candidate revisions.
4. Port local changes deliberately and update the table above with a full SHA.
5. Run `./tools/validate.sh`, then repeat the affected hardware acceptance
   cases. A build-only result is insufficient for display, portal, Recovery, or
   power claims.
6. Update `THIRD_PARTY_NOTICES.md`, copied license text, acceptance evidence,
   and release SBOM if ownership or dependencies changed.

Do not copy `.git` directories, upstream build outputs, credentials, or device
dumps into this repository.
