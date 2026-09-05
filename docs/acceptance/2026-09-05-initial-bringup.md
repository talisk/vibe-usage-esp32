# Initial bring-up and acceptance record — 2026-09-05

Historical 0.1.0 record. The [authenticated continuation](2026-09-05-authenticated-bringup.md)
supersedes current status and corrects the daily API-boundary inference below.

This is the authoritative evidence ledger for the initial `vibe-usage-esp32`
implementation. It deliberately separates automated, API, installation,
device, Recovery, and power evidence. `NOT RUN` is not a failure, but the
corresponding capability is not claimed as hardware-validated.

## Build identity

| Item | Recorded value |
| --- | --- |
| Source | Initial uncommitted workspace; local `v0.1.0` package; no release tag or CI run |
| Toolchain | ESP-IDF 5.5.3 |
| Dependency locks | `firmware/ai-passport/dependencies.lock`, `firmware/zectrix/dependencies.lock` |
| Passport target | ESP32-C3, configured 8 MB flash, no PSRAM |
| NOTE4 target | ESP32-S3, configured 16 MB flash, 8 MB octal PSRAM |
| Artifact checksums | `dist/v0.1.0/SHA256SUMS`; manifest covers 41 payload files |

## Evidence summary

| Category | Status | Evidence and boundary |
| --- | --- | --- |
| Build | PASS | Clean dual release build passed with ESP-IDF 5.5.3; exact sizes below |
| Host tests | PASS | ASan/UBSan core suite and ten Python layout/config/plan tests passed |
| CI | NOT RUN | Workflows exist, but no remote run exists |
| Real Vibe API | PARTIAL | Read-only contract probe passed; the final NOTE4 release reached TLS-validated Device Flow, rendered a QR, and handled its real 15-minute expiry; no authorization was approved, so credential persistence and GET remain pending |
| Device tests | PARTIAL | Final NOTE4 application, PSRAM, RTC, full/partial e-paper, Wi-Fi and Device Flow ran through a 15-minute expiry without a crash; Passport did not enumerate |
| Installer tests | PARTIAL | Both self-contained release-package dry-runs passed; final NOTE4 segmented write and exact-length write-back hashes passed; Passport write not run |
| Recovery tests | NOT RUN | Build-time hook check is not a real Recovery boot |
| Power tests | NOT RUN | No current, battery, RTC-alarm, or latch-cycle measurement |

## Automated evidence

The repository gate covers deterministic dates, aggregation, exact integer
boundaries, source collapse, cache CRC/version/generation, incomplete windows,
parser chunk boundaries and malformed input, Device Flow JSON shape and string
safety (including raw/escaped NUL, exact parse boundaries, trusted origins, and
header-safe API keys), pagination fail-closed behavior, HTTP policy, lifecycle
reducer behavior, partition MD5/layout, protected-region image limits, Python
syntax, shell syntax, clean dual builds, image size, and selected stack-frame
budgets. The final clean gate reported:

- repository contracts: 166 source files;
- host core suite: PASS; Python layout/config/flash-plan suite: 10 tests, PASS;
- Passport app: 1,730,688 / 3,145,728 bytes; merged end `0x1b6880`;
- NOTE4 app: 1,299,936 / 3,145,728 bytes; merged end `0x14d5e0`;
- stack frames: `decode_cache_into=80 B`, `fetch_today=3264 B`,
  `poll_device_link=3264 B`, `reconcile_one=128 B`, `vibe_cache_decode=32 B`;
- clean merged-image SHA-256: Passport
  `e1c0e54af3b52b47c2ba41b85a2a7b5d135e21e8db87684902f80959bca2225e`;
  NOTE4
  `106fd88f2241edeb8a54b67f3709c9491a24988e98df20d578229c07c689c189`;
- release metadata: 43 checksummed files; artifact privacy scan: 44 files; every entry in
  `SHA256SUMS` verified.

Two independent clean release runs produced byte-identical bootloader,
partition-table, application, and merged-image SHA-256 values for both boards.
The release-package content gate also checked required documentation,
third-party license texts, sanitized fixtures, bundled installer tools, and all
local Markdown links.

Both clean sdkconfigs selected reproducible builds, disabled Bluetooth, and
selected the expected chip and flash size. NOTE4 additionally selected octal
PSRAM. GitHub Actions syntax and expressions passed `actionlint` v1.7.7;
workflow execution remains a separate CI item.

The host suite does not emulate ESP NVS power failure, a TLS server, the captive
portal radio, or an e-paper panel. Those remain hardware or integration items.

## Real API contract evidence

A read-only request using an existing local credential was performed without
printing or saving the credential, origin details beyond the public bootstrap,
source/project/host names, response bodies, or real totals. It observed:

- an apparent exclusive next-calendar-day `to` boundary in a sparse sample;
  **this inference was disproved by the authenticated continuation**;
- `days=1` was not equivalent to the chosen local day;
- a complete empty range may coexist with account-level `hasAnyData=true`;
- no pagination metadata in the sample, which is not proof it can never occur;
- bucket `totalTokens` matched input + output + reasoning for the sample and can
  differ from an app total that also counts cached input.

The sanitized facts and synthetic fixture are in
[`../api-contract.md`](../api-contract.md) and `tests/fixtures/`. This is API
contract evidence, not proof that either firmware completed Device Flow or a
valid-account GET.

## Device inventory and actions

| Device | Power/connection | Read-only identity | Write/boot result |
| --- | --- | --- | --- |
| ZECTRIX NOTE4 BLACK-WHITE | USB, macOS serial | ESP32-S3 QFN56 rev 0.2; 16 MB flash; 8 MB embedded PSRAM | Final release segments written and hash-verified; app `0.1.0` (`07d9ec7ae121b8761e6465504d8f0f56ca8816bac8da7104aaec7c09e0ba75fb`) booted |
| FoloToy AI Passport | Expected USB | No serial device enumerated during initial inventory | NOT RUN; no write attempted |

The NOTE4 installer wrote only bootloader `0x0`, partition table `0x8000`, and
factory app `0x10000`; esptool verified the hash of each write. No whole-chip
erase, protected partition read, eFuse operation, or protected partition write
was performed. An independent read-back comparison covered exactly bootloader
`0x5130`, partition table `0xc00`, and application `0x13d5e0`, and all three
matched the candidate bytes. Serial evidence was reviewed without retaining
device identifiers, network names, addresses, Device Flow codes, or Usage data
in this record.

## Hardware matrix

`PARTIAL` means only the stated subcondition has evidence. P1 items are recorded
to prevent accidental overclaiming; they do not block the documented P0
constant-power mode unless explicitly listed as a P0 condition.

| ID | Board | Status | Observation / remaining evidence |
| --- | --- | --- | --- |
| HW01 | Both | PARTIAL | NOTE4 chip/flash/PSRAM and 400x300 display runtime passed; Passport still requires enumeration/runtime identity |
| HW02 | Both | NOT RUN | Empty-Wi-Fi portal on iOS and Android |
| HW03 | Both | NOT RUN | Wrong password, correction, retained networks, hostile/boundary SSIDs |
| HW04 | Both | PARTIAL | NOTE4 reconnected to one preserved WPA2 network; hidden SSID, duplicate AP, priority and 5 GHz-only cases remain |
| HW05 | Both | NOT RUN | No internet, DNS failure, and SNTP failure differentiation |
| HW06 | Both | NOT RUN | Portal timeout/cleanup, heap recovery, no STA management listener |
| HW07 | Both | PARTIAL | NOTE4 validated the TLS chain, created a real Device Flow and rendered its QR; the final candidate received no approval before expiry, so authorization persistence and first GET 200 remain |
| HW08 | Both | PARTIAL | NOTE4 cleared a real expired code at 15 minutes and the final image switched to the actionable error/retry page; an earlier controlled restart did not restore an unfinished code. Denial, 410, slow-down/429 pacing, and physical OK retry remain |
| HW09 | Both | NOT RUN | Revoked credential 401, tombstone, preserved Wi-Fi, relink |
| HW10 | Both | NOT RUN | Cross-account generation isolation |
| HW11 | Both | NOT RUN | Empty account, empty today with history, multi-source UI |
| HW12 | Both | NOT RUN | On-device Today/7D versus sanitized raw integer reconciliation |
| HW13 | Both | NOT RUN | Late data, downward revision, midnight, two-day offline recovery |
| HW14 | Both | NOT RUN | 429/500/TLS/partial/limit failures retaining LKG on device |
| HW15 | Both | PARTIAL | NOTE4 controlled restart preserved and reused Wi-Fi NVS and RTC time; cache persistence and interrupted-write recovery remain |
| HW16 | Both | NOT RUN | Maximum auth/cache and 1000 logical NVS updates in 24 KiB |
| HW17 | Both | NOT RUN | Twenty portal/settings cycles and leak/socket/task audit |
| HW18 | Passport | NOT RUN | Real TLS + LVGL + large response internal-heap thresholds |
| HW19 | Passport | PARTIAL | Build verifier enforces app/layout/hook; vendor installer and real UP Recovery boot not run |
| HW20 | Passport | NOT RUN | Pre/post install/reset protected-region hash comparison; no dumps retained |
| HW21 | Passport | NOT RUN | Battery missing/read failure and 30-second dim/wake behavior |
| HW22 | Passport P1 | NOT RUN | Battery screen-off and supported wake path |
| HW23 | NOTE4 | PARTIAL | On the final candidate, boot/page full refreshes and bounded partial refreshes passed; after ten partial changes the next changed frame forced a full refresh, then the following minute change restarted the partial counter at one. The 100-change visual ghosting sequence remains |
| HW24 | NOTE4 | PARTIAL | On the final candidate, a real Device Flow QR rendered; a complete 15-minute wait showed minute-bounded countdown refreshes, automatic full refresh after the partial limit, and a full refresh to actionable expiry visual 11 at about 932 seconds. Three successful scans remain |
| HW25 | NOTE4 | NOT RUN | BUSY timeout/partial failure then full recovery |
| HW26 | NOTE4 | NOT RUN | Three-second DOWN shutdown on USB and battery |
| HW27 | NOTE4 P1 | NOT RUN | 48 real 30-minute battery wake cycles |
| HW28 | NOTE4 P1 | NOT RUN | Latch rail, sleep current, peak, and cycle energy |
| HW29 | NOTE4 P1 | NOT RUN | Offline/reconciliation wake budgets and no persistent AP |
| HW30 | NOTE4 P1 | NOT RUN | PCF8563 alarm on GPIO5 and invalid-RTC recovery |
| HW31 | NOTE4 P1 | NOT RUN | Deep-sleep first full and shadow contract |
| HW32 | Both | PARTIAL | The 44-file self-contained release and retained evidence privacy audits passed; a sanitized NOTE4 observation counted zero MAC, IPv4, URL, or connected-SSID pattern lines. Upgrade/migration/interrupted-reset cases remain |

## Release decision

The local `v0.1.0` package is build-qualified and has partial NOTE4 bring-up
evidence; it is not a public hardware-qualified release. At minimum, each board
still needs HW02, HW06, completion of HW07, HW11, HW12, HW14, HW15, and its
remaining display/install cases before a P0 hardware claim. Passport additionally
needs enumeration, installation, and real Recovery preservation evidence.

P1 battery wake, RTC-alarm, and energy items remain explicitly outside the P0
constant-power firmware claim.
