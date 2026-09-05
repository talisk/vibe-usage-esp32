# Product branding — 0.0.1 / 2026-09-05

This record supersedes the current candidate in
[photo-driven UI fixes](2026-09-05-ui-fixes.md), without changing historical
evidence. At the owner's request, the product version starts at 0.0.1; earlier
0.1.x entries are internal-build history, not published releases.

## Changes

- Formal names: Vibe Passport and Vibe Note. About headings intentionally use
  `VIBE PASSPORT` and `VIBE Note`.
- Settings account entry: `VibeCafe` on both products.
- About includes `https://github.com/talisk/vibe-usage-esp32` and
  `Author: https://x.com/SwainTalisk`. Passport splits the repository URL at
  the owner/path boundary into two complete lines, without ellipsis.
- Root `VERSION` is the single source for both ESP-IDF app descriptions,
  About and HTTP User-Agent. Release tooling rejects a mismatched tag/version.
- Passport: black root, clipped 24px-radius child surface, inset vector ↑/↓
  footer arrows. The 64 KiB bounded LVGL pool accommodates ARGB corner clipping
  layers without PSRAM; the host renderer uses the same pool size.
- Vibe Note's setup AP/hostname prefix is now `VibeNote`; saved Wi-Fi is not
  reset. Board identifiers, partition layouts and Recovery filenames stay fixed.

No service upload, repository creation or publication is performed. Auth and
settings remain schema 1; usage cache remains schema 2. This version change
does not migrate or clear user data.

## Verification

Real LVGL RGB565 rendering checks all four black outer corners with an
intentionally overflowing white child, a visible center, and actual ink in
both inset arrow regions. Real Montserrat metrics check all About lines and
existing compact fields. Vibe Note's locked proportional font metrics check
the full repository and author URLs, name, version, menu and overview title.
These are automated geometry/rendering checks, not a physical-panel visual
acceptance or confirmation of the exact bezel radius.

The full `tools/validate.sh` gate passed with ESP-IDF 5.5.3: dual clean builds,
protected layout, stack budgets, core sanitizers, ten Python verifier tests,
and actual LVGL rendering. Final static validation additionally includes the
new Vibe Note font test, version contract and local workflow lint (181 source
files). A deliberately mismatched `release.sh 0.0.2` was rejected before build
or publication. This does not validate a remote CI/release run.

## Candidate and device evidence

Source is the initial uncommitted workspace on unborn `main`, not a release
tag. Both app descriptions report 0.0.1. Clean merged images are byte-identical
to the incremental builds used for flashing.

| Product / hardware | Chip / memory | App bytes | Application SHA-256 |
| --- | --- | --- | --- |
| Vibe Passport / AI Passport | ESP32-C3, 8 MB, no PSRAM | 1743968 | `39ffefc40fa2aa0174993716ef537ed9a5306533894eca9141880dd166f9e475` |
| Vibe Note / NOTE4 BLACK-WHITE | ESP32-S3, 16 MB, 8 MB PSRAM | 1301216 | `2467b29360a96fefc0bca4576c801b9f424b3afa499a1b84a4fbe5959467ed67` |

Merged SHA-256:

- Passport: `105a353119a0097b5c9ba662d35938bb782a4ab0196f1442efd1fb746b786828`.
- Note: `9ef7f7800db91f8310c78350b0eab2427dd8ff6c33cdfb09f9d0516687685792`.

Both boards were USB-powered. Before each segmented write, ports were
rediscovered and the installer confirmed the expected chip and flash size
read-only. Bootloader, partition table and factory app writes passed device
hash verification. No NVS erase, protected-region access, eFuse operation,
account reset or cache-schema migration occurred.

Opening the serial observer caused a normal reboot on both boards. Sanitized
logs confirmed version 0.0.1, `auth=1 cache_restored=1 error=OK`, valid TLS
certificates, and fresh `state=9 http=200 error=OK today_complete=1 coverage=7/7
persisted=1/1`. Passport reported display/LVGL initialization; Note completed
a full dashboard refresh. After the first sync, Passport reported 72,716 bytes
free internal heap, a 27,648-byte largest block and 4,316 bytes free controller
stack. These are point-in-time diagnostics, not worst-case memory guarantees.

| Category | Status | Scope |
| --- | --- | --- |
| Build | PASS | Both clean builds, equality, layout and stack budgets |
| Host tests | PASS | Core/Python, both fonts, Passport corner/arrow pixels |
| CI | NOT RUN | Local lint only; no remote workflow |
| Real API | PASS, scoped | Authenticated post-upgrade GET 200 and 7/7 on both |
| Device tests | PARTIAL | Boot, saved state, TLS/sync; visual recheck pending |
| Installer tests | PASS, scoped | Segmented writes and device hash verification |
| Power tests | NOT RUN | USB only; no battery/current or latch verification |

## User recheck

1. Open Settings on both devices and confirm the `VibeCafe` entry.
2. Open About and check the exact product title, Version 0.0.1, repository
   spelling and author URL. Passport's URL spans two lines intentionally.
3. On Passport Overview, Agents, Status and Settings, confirm both ↑ and ↓
   appear fully, the center footer stays readable, and every corner is black
   outside the rounded surface. Confirm no bright slivers remain at the bezel.
4. Browse multiple pages and return from About; check for missing content,
   flicker or rendering stalls. Account and current usage should remain intact.

Physical visual review, adverse network cases, Recovery, abrupt power loss,
long-run ghosting, battery/current and power-latch tests remain open unless
explicitly recorded with new evidence.
