# Authenticated bring-up continuation — 2026-09-05

For the newer 0.1.2 UI candidate, see [photo-driven UI fixes](2026-09-05-ui-fixes.md).

This is the current evidence ledger. It supersedes current-state claims in the
[initial 0.1.0 record](2026-09-05-initial-bringup.md), whose detailed hardware
matrix remains open except for the explicit updates below. It is not a claim
that the complete P0 or P1 acceptance matrix has passed.

## Evidence summary

| Category | Status | Boundary |
| --- | --- | --- |
| Build | PASS | Clean 0.1.1 builds for both chips; layout and stack budgets passed |
| Host tests | PASS | ASan/UBSan core suite, 10 Python tests, 169-file repository check, shell/Python syntax and local actionlint |
| CI | NOT RUN | No remote workflow run or release tag |
| Real API | PASS, scoped | Seven-day live API/core integer reconciliation; both boards authenticated GET 200 and 7/7 |
| Device tests | PARTIAL | Both normal boots, authorization/cache persistence and online synchronization; physical and adverse-condition matrix open |
| Installer tests | PARTIAL | Both segmented writes verified; clean release images match flashed application hashes; vendor Recovery installation not run |
| Recovery / Power | NOT RUN | No protected-region read-back, real Recovery boot, battery current or wake-cycle measurements |

The clean package contains 45 files including 44 checksum entries. Firmware
hashes below match both the flashed incremental build and independent clean
release build. The final acceptance document is refreshed during packaging;
metadata, local links, privacy, and checksums are regenerated and checked after
that refresh. This is a local candidate, not a public hardware-qualified release.

## Identity and implementation

- Source: uncommitted initial workspace, firmware 0.1.1; no tag or remote CI run.
- Toolchain: ESP-IDF 5.5.3, pinned dependencies and reproducible-build settings.
- Passport: ESP32-C3 QFN32 rev 1.1, embedded 8 MB XMC flash, no PSRAM, USB power.
- NOTE4 BLACK-WHITE: ESP32-S3 QFN56 rev 0.2, 16 MB flash, 8 MB PSRAM, USB power.
- Passport application: 1,731,648 bytes, SHA-256
  `66dd97aacc1da22c366951b5243088a5010825cec580d6f20a46f4fd9b694d21`.
- NOTE4 application: 1,300,768 bytes, SHA-256
  `14d13851f383ccec142684a7c79de689e83774056a1492398c85023bbcde65df`.

The installer rediscovered ports and checked chip/flash identity before each
write. Only bootloader, partition table, and application were written; NVS,
Passport card identity, and Recovery were neither dumped nor erased. esptool
verified each written segment. Protected-region preservation here is supported
by write-range validation, not a claimed protected-region read-back hash test.

## Real API defect and fix

An authorized NOTE4 initially restored its credential and cache, validated TLS,
and received HTTP 200, but rejected historical candidates as `OUT_OF_RANGE`.
Coverage remained 2/7 while Last Known Good was retained.

Read-only live probes found that an exact next-midnight `to` includes the bucket
at that instant. Naked date bounds also omitted early local-day buckets in the
sample. The former exclusive-boundary inference was therefore incorrect.
Version 0.1.1 sends explicit UTC instants spanning local midnight through the
final millisecond of the same local date. Cache schema 2 invalidates potentially
incomplete old caches while retaining schema-1 authorization and device config.

`tools/probe_live_usage.py` fetched all seven current Asia/Shanghai dates using
the production date helper. It fed each body through the production streaming
parser in 113-byte chunks, compared exact daily integers against an independent
Python sum, then checked the production cache snapshot's Today, 7D, and 7/7 mask.
All seven HTTP responses and all comparisons passed. Existing host credentials
were used only in memory; no keys, raw responses, source names, or real totals
were stored. This is a real API + host-core check, not an independent reading of
the board's physical display or proof that host and device use the same account.

## Observed hardware progress

### NOTE4

After the user's Device Flow approval, both diagnostic and 0.1.1 firmware
restored `auth=1`. On 0.1.1, a subsequent observed boot restored cache 7/7 and
both persisted flags; it reconnected to saved Wi-Fi, validated the certificate,
completed a fresh GET with HTTP 200 / `OK`, and entered dashboard state 9 with
Today complete and coverage 7/7. No panic or watchdog appeared in this sample.

The diagnostic firmware reported only 732 bytes of controller stack headroom.
Moving a large scheduler temporary into the controller-owned scratch view raised
the observed 0.1.1 low-water mark to 3,724 bytes after TLS/GET. At that point free
internal heap was 153,371 bytes and the largest block 36,864 bytes. This is a
short functional sample, not a maximum-response or long-run resource test.

### Passport

The reconnected device was identified and diagnostic firmware was safely
installed. Runtime initialized the 240x320 display, GPIO21 backlight, ADC button
driver, and controller. It reported no stored authorization/cache and entered
Wi-Fi provisioning state 3. The user then completed provisioning and approval:
the next diagnostic boot restored authorization and a partial cache. After a
safe 0.1.1 upgrade, the device retained authorization and Wi-Fi, validated TLS,
received HTTP 200 / `OK`, and filled the rebuilt cache to 7/7 with both persisted
flags at about 31 seconds into the observed boot. At that point internal heap
was 112,332 bytes, largest internal block 57,344 bytes, and controller stack
low-water mark 4,364 bytes. No panic or watchdog appeared in this short sample.
These post-request heap measurements do not establish the minimum during a TLS
handshake or the maximum-response/24-hour HW18 gate. Physical pixel quality,
buttons, and Recovery are not inferred from driver initialization logs.

A subsequent observed Passport boot restored `auth=1`, `cache_restored=1`,
Today complete, coverage 7/7, and both persisted flags before networking.
It then validated TLS and completed another HTTP 200 / `OK` GET with 7/7
coverage; the stack low-water mark in this second boot was 4,308 bytes.
Serial reopening caused a normal boot in this setup; this is restart evidence,
not a battery-removal or interrupted-write power-loss test.

## Acceptance deltas and remaining work

| Check | Updated evidence | Still required |
| --- | --- | --- |
| HW01 | Both chip/flash identities and board initialization observed | Physical display/button checks |
| HW04 | Both retained Wi-Fi and reconnected after upgrade | Adverse Wi-Fi matrix |
| HW07 | Both approved credentials restored; valid GET 200 and 7/7 dashboard state | Physical code/screen confirmation |
| HW12 | Live seven-day API independently matches host firmware core | On-device Today/7D versus independent same-account totals |
| HW14 | Real boundary-error candidates retained LKG before fix | Injected 429/500/TLS/partial/limit failures on both devices |
| HW15 | Both auth, Wi-Fi, and schema-2 7/7 cache survive restart | Interrupted writes and abrupt power removal |
| HW18 | NOTE4 short-run heap/stack sample improved | Passport TLS + LVGL worst-case resource gate |
| HW19/20 | Passport segmented safe write and normal boot | Real UP Recovery boot and explicitly authorized hash-only checks |
| HW32 | Both schema-1 to schema-2 upgrades retain authorization | Interrupted reset/migration and remaining privacy matrix |

All other NOT RUN/PARTIAL rows in the initial ledger remain unchanged. In
particular, phone OS portal cases, forced error injection, empty/multi-account
cases, long-run NVS/heap/EPD sequences, real Recovery, and battery/power tests are
not completed. Do not interpret authenticated NOTE4 success as full hardware
acceptance, or local workflow lint as a remote CI run.
