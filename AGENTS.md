# Repository guidance for coding agents

Read this file before changing the repository. Then read only the task-specific
documents named below.

## Non-negotiable safety contracts

- Targets are FoloToy AI Passport (`esp32c3`, 8 MB, no PSRAM) and ZECTRIX
  NOTE4 BLACK-WHITE (`esp32s3`, 16 MB, 8 MB octal PSRAM). NOTE4C is unsupported.
- Never run or recommend `erase-flash` for either device.
- Passport partitions are fixed: NVS `0x9000+0x6000`, factory app
  `0x10000+0x300000`, factory-owned card identity `0x356000+0x4000`, and
  permanent Recovery `0x700000+0x100000`. Never read out, package, erase, or
  overwrite identity/Recovery data. Preserve the five-second UP boot hook.
- P0 must not burn eFuses or enable Secure Boot, Flash Encryption, custom OTA,
  or unverified deep-sleep scheduling.
- Before every flash, rediscover serial ports and identify the chip read-only.
  Never reuse a port assumption from an older run.
- Keep Build, Host tests, CI, real API, Device, Installer, and Power evidence
  separate. Only mark a check PASS when the matching evidence exists.

## Source and architecture boundaries

- Product names are Vibe Passport and Vibe Note; About headings intentionally
  use `VIBE PASSPORT` and `VIBE Note`. Hardware names and build paths remain
  unchanged. Keep branding in `vibe_product.h` and version in root `VERSION`;
  both builds and HTTP User-Agent must derive their version from it.
- Passport uses a black root and a 28px rounded/clipped child surface. Keep
  footer arrows inside the safe area and run real LVGL pixel/font regressions
  after changing its layout; no optional Unicode arrow glyph dependency.
- UI languages live in `components/vibe_ui/messages.tsv`; regenerate all font
  subsets after changing CJK copy. Keep `settings_v1` binary-compatible; language
  is an independent `language_v1` byte, default English. Run catalog, canvas,
  LVGL, and config migration/failure tests before delivery.
- Use a shared font/baseline for Latin and CJK. Never center or resize each
  glyph by its ink bounds; regenerate and check no-crop/width regressions.
  About QR codes need four white quiet-zone modules and a full Note refresh
  when entering/leaving the QR view or changing its target.
- Shared protocol, parser, aggregation, cache, and storage code lives in
  `components/vibe_usage`; it must not depend on LVGL, EPD, or board GPIO.
- `components/app_controller` is the single owner of serialized network and
  lifecycle work. Button/display callbacks must remain non-blocking.
- Board UI and power behavior stay under the matching `firmware/*` target.
- Wi-Fi behavior is wrapped by `components/wifi_adapter`. Changes to the
  vendored component must be minimal, auditable, and recorded in
  `docs/upstream-lock.md`.
- LVGL calls require the Passport BSP LVGL lock. NOTE4 refreshes must honor the
  driver's base/shadow contract and may only mark a frame rendered after the
  driver reports success.
- Preserve user changes. Begin with `git status --short --branch`; do not clean,
  reset, or overwrite unrelated work. Commit or push only when requested.

## Data, security, and privacy

- Never add API keys, Authorization values, Wi-Fi passwords, cardid bytes,
  personal source/project/session data, or raw personal Usage responses to
  code, fixtures, logs, screenshots, or release artifacts.
- Device Flow is the only supported account bootstrap. UI code must not receive
  the API key.
- Usage candidates replace a day only after a complete, bounded, schema-valid
  response. Errors retain Last Known Good and never become a successful zero.
- Preserve exact uint64 parsing and checked addition. The metric is
  `API_TOTAL_V1`; changing it requires a new metric/schema version and cache
  invalidation.
- A 401 first persists a higher-generation tombstone, invalidates current RAM
  use, and stops old-key work. A storage failure must remain visible.
- Never introduce TLS verification bypasses, redirects, credential logging,
  or whole-NVS erase fallback. Usage API origins remain fixed. Explicitly
  configured LLM origins follow ADR 0002 and must never receive Usage keys.

## Context routing

For a user-authorized firmware flash, use the repository skill at
[`.agents/skills/flash-firmware/SKILL.md`](.agents/skills/flash-firmware/SKILL.md).
It reuses the existing verified installer; skill creation or inspection alone
does not authorize writing a connected device.

| Task | Read first |
| --- | --- |
| Protocol, parser, metric, dates | `docs/api-contract.md`, `docs/architecture.md` |
| Partitions, build, release, flashing | `docs/installation.md`, `docs/upstream-lock.md` |
| Wi-Fi or captive portal | `docs/architecture.md`, the Wi-Fi section of `README.md` |
| Hardware/UI/power | matching file under `docs/upstream/`, then neighboring BSP/UI source |
| Validation or completion | latest file under `docs/acceptance/` |

The original implementation plan is preserved at
`docs/VIBE_USAGE_HARDWARE_FIRMWARE_PLAN.zh_CN.md` for detailed acceptance IDs.

## Validation

Run the smallest relevant check while iterating and the full gate before a
delivery that changes firmware:

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

Hardware tests must record firmware source revision, image checksum, board,
chip, flash/PSRAM facts, power source, exact action, expected result, observed
result, and sanitized evidence. Never store a protected partition dump merely
to prove its hash; compare read-only hashes in memory/terminal and record only
the masked result when explicitly authorized.

## Delivery format

Report at least:

```text
Build: PASS / FAIL / NOT RUN
Host tests: PASS / FAIL / NOT RUN
CI: PASS / FAIL / NOT RUN
Real API: PASS / FAIL / NOT RUN
Device tests: PASS / FAIL / NOT RUN
Installer tests: PASS / FAIL / NOT RUN
Power tests: PASS / FAIL / NOT RUN
Unverified: concrete remaining checks
```
