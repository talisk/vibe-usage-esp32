# Changelog

All notable changes to this project will be documented here. The project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.0.1] - 2026-09-05

- Align Latin/CJK on one Noto Sans baseline without glyph clipping or distortion;
  balance Note monochrome strokes and test the actual replacement font metrics.
- Add localized Repository/X QR views to About (UP/DOWN; OK returns), preserving
  integer module scaling, white quiet zones and Note full-refresh transitions.
- Add persisted English, Simplified Chinese, Traditional Chinese and Japanese
  UI selection, bundled OFL CJK subsets, and a Language Settings entry.
- Add localized public project details to Wi-Fi setup and success pages.
- Shorten Reset Settings, wrap Note's setup timeout copy, fit the account
  password notice, and enlarge Passport corners from 24px to 28px.
- Test language/config migration, failure paths, UTF-8/font coverage, actual
  Note canvas inversion and both boards' compact text geometry.

- Establish Vibe Passport and Vibe Note product names, VibeCafe settings,
  product-specific About pages, repository and author links.
- Reset the product version to 0.0.1 at the owner's request. Both builds,
  HTTP User-Agent and release validation share the root VERSION file.
- Draw inset Passport up/down arrows without font-glyph dependencies and
  clip the 24px rounded surface against a black background.
- Add actual RGB565 corner/arrow rendering and branding typography tests.

The entries below describe earlier internal builds, not published releases.
Their original versions and device evidence are preserved for auditability.

## [0.1.2] - 2026-09-05 (historical internal build)

### Fixed

- Bound Passport single-line labels; reserve nonoverlapping header, footer,
  ranking and status columns, and give intentional paragraphs explicit height.
- Replace unsupported middle-dot separators with ASCII; use compact 12px fonts
  for dense metadata and support P/E token suffixes for full uint64 values.
- Put NOTE4 overview bars below text and allow scrolling through every source
  in the Agents view; clamp list offsets after window/count changes.
- Derive Ready/Empty/Stale from the selected window's completeness and total,
  and distinguish Today and 7D state in Status.

### Tests

- Real LVGL host label-height and font-width checks, selected-window state
  regressions, and list edge/offset tests for both six- and eight-row layouts.
- No cache, authorization, Wi-Fi, partition, or Recovery migration is required.

## [0.1.1] - 2026-09-05

### Fixed

- Use explicit UTC day bounds and the API's inclusive final millisecond to
  include local morning data and exclude the next day's midnight bucket.
- Invalidate old date-only usage caches with cache schema 2 while preserving
  account authorization and Wi-Fi configuration.
- Remove the scheduler's large stack-local view so nested TLS and time-sync
  calls have more stack headroom.

### Added

- Privacy-safe authorization, HTTP, cache-coverage, and memory-health logs for
  real-device acceptance, without credentials, source names, or token totals.

## [0.1.0] - 2026-09-05

### Added

- Shared bounded Vibe Device Flow, HTTPS Usage client, exact `API_TOTAL_V1`
  aggregation, eight-day CRC cache, and resumable local reset.
- FoloToy AI Passport LVGL firmware with protected Recovery-compatible layout.
- ZECTRIX NOTE4 BLACK-WHITE firmware with 1-bpp full/partial EPD rendering, RTC
  integration, and clean latch shutdown.
- Audited captive portal/station flow, dual clean-build tooling, partition and
  merged-image verifier, stack budgets, release manifest/checksums/SPDX, and
  open-source project documentation.
- Self-contained release archives with documentation, third-party license
  texts, sanitized fixtures, a verified installer, and local-link validation.

### Fixed

- Reject embedded NUL characters in Usage and Device Flow JSON keys and values
  before C-string consumers can truncate them, and require Device Flow parsing
  to end exactly at the HTTP body boundary.
- Reject malformed Device Flow success shapes, non-string returned origins,
  untrusted/control-bearing verification URLs, and API keys outside the pinned
  `vbu_` URL-safe format.
- Route expired or denied Device Flow states to an actionable retry screen on
  both targets.
- Make NOTE4 full/partial/skip refresh selection independently host-testable,
  including byte-aligned dirty regions and the ten-partial full-refresh limit.

### Validation boundary

- Compilation and host-test status is recorded separately from real API,
  hardware, installer, Recovery, and power status in
  [`docs/acceptance/2026-09-05-initial-bringup.md`](docs/acceptance/2026-09-05-initial-bringup.md).
