# Optimization Recommendations — 2026-10-10

Analysis-only report for the Vibe Passport / Vibe Note firmware repository
(head `ce974c8`, ESP-IDF v5.5.3). **No code was changed, no hardware was
flashed.** Every claim below is labeled **[measured]** (verified from source,
compiled, or checked against upstream configuration), **[hypothesis]**
(plausible but unverified on hardware), or **[rejected]** (investigated and
found not worth doing, with the reason).

Scope note: all suggestions respect the repository's non-negotiable
constraints — fixed Passport partitions, no eFuses/Secure Boot/custom OTA, no
`erase-flash`, vendored `esp_wifi_connect` kept frozen, `vibe_usage` free of
LVGL/EPD/GPIO dependencies, and `app_controller` as the single serialized
lifecycle owner.

---

## Priority summary

| # | Recommendation | Benefit | Effort | Confidence |
|---|---|---|---|---|
| 1 | Enable compiler optimization (`-Os`) on both targets | Smaller/faster firmware for one line each | Trivial | High |
| 2 | Set a Wi-Fi power-save policy for station mode | Lower battery drain (unverified) | Small | Medium |
| 3 | Skip redundant copies of the ~4.9 KB view struct | Less recurring CPU/mutex work | Medium | High (cost), Medium (win) |
| 4 | Batch reconcile-day NVS writes | Less flash wear, faster reconcile | Small–Medium | High |
| 5 | Record voice scratch in PSRAM on Note4 | Zero flash wear per voice command | Medium | Medium |
| 6 | Share the duplicated per-board UI page/menu model | Halve maintenance surface for UI changes | Large | High (duplication), Medium (plan) |
| 7 | Add parser fuzzing + host coverage to CI | Early defect detection in the most exposed code | Small | High |
| 8 | Passport idle strategy beyond backlight dimming | Battery life (unverified) | Large | Low until measured |
| 9 | Micro-fixes: battery-poll re-render, aggregate source index | Small CPU savings | Trivial | High |

---

## 1. Enable compiler optimization on both targets — trivial, high value

**[measured]** Neither `firmware/ai-passport/sdkconfig.defaults` nor
`firmware/zectrix/sdkconfig.defaults` sets any
`CONFIG_COMPILER_OPTIMIZATION_*` option, and no `CMakeLists.txt` overrides
`-O` flags. ESP-IDF v5.5.3's top-level `Kconfig` defaults the
`COMPILER_OPTIMIZATION` choice to `COMPILER_OPTIMIZATION_DEBUG` (`-Og`)
(verified against the `v5.5.3` tag). So both production builds compile at
`-Og`, which is a debug-oriented level.

- **Change:** add `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` (`-Os`) to both
  `sdkconfig.defaults`. Per Espressif's own option help, `-Os` produces code
  that is "smaller **and faster**" than the debug default. `-O2`
  (`CONFIG_COMPILER_OPTIMIZATION_PERF`) is the alternative if speed matters
  more than size; the 3 MiB factory-app cap argues for `-Os` first.
- **User benefit:** smaller binaries (headroom under the fixed 3 MiB app
  partition) and faster hot paths (streaming parser, aggregation, LVGL
  rendering on Passport). Exact gains are **[hypothesis] until measured** —
  record image sizes before/after.
- **Tradeoffs:** optimized frames differ from debug frames, so
  `tools/check_stack_usage.py` budgets must be re-verified (it is already part
  of `./tools/validate.sh --firmware`); crash backtraces are slightly harder
  to read.
- **Validation:** `./tools/validate.sh --firmware` (build + stack budget +
  UI layout tests), plus a before/after comparison of the merged image sizes
  recorded in the build evidence.

## 2. Set an explicit Wi-Fi power-save policy — small effort, likely battery win

**[measured]** The vendored component already exposes
`WifiStation::SetPowerSaveLevel` mapping LOW/BALANCED/PERFORMANCE to
`WIFI_PS_MAX_MODEM`/`WIFI_PS_MIN_MODEM`/`WIFI_PS_NONE`
(`components/esp_wifi_connect/wifi_station.cc:349-366`) and forces
`WIFI_PS_NONE` while the captive portal is active
(`components/esp_wifi_connect/wifi_configuration_ap.cc:230`). A repository-wide
grep confirms **no product code ever calls it**: neither
`components/wifi_adapter` nor `app_controller` sets a station power-save
policy, so the radio runs on the IDF default with no explicit intent.

- **Change:** from `wifi_adapter` (after the portal closes / station connects),
  call the existing API with BALANCED. This adds **zero** vendored-code delta,
  keeping `docs/upstream-lock.md` clean.
- **User benefit:** modem-sleep between beacons reduces average current.
  Magnitude is **[hypothesis]** — no board in this repo has power evidence for
  it yet; do not claim numbers until measured.
- **Tradeoffs:** modem sleep adds latency to TLS/HTTP round trips, which may
  slightly slow a manual refresh (rate-limited to 1/min anyway). Portal and
  Device-Flow linking windows should keep `WIFI_PS_NONE` (the vendored code
  already handles the portal case).
- **Validation:** Device power tests per `AGENTS.md`: same board, same power
  source, measured current idle vs. with policy, recorded under
  `docs/acceptance/`; confirm refresh latency stays acceptable.

## 3. Stop copying the full ~4.9 KB view struct on every UI wake — medium effort

**[measured]** `sizeof(app_controller_view_t) == 4920` bytes on the host
compiler (compiled from `components/app_controller/include/app_controller.h`;
dominated by `verification_uri[1024]`, two 936-byte `vibe_usage_snapshot_t`s,
and 12×120-byte todo items). `read_view()`
(`components/app_controller/app_controller.c:149-157`) does
`memset` + full struct copy under the view mutex, and it is called on **every**
UI loop wake: Passport every ≤100 ms plus per queued button
(`firmware/ai-passport/main/vibe_passport_ui.c:1092-1138, 1113`), Note4 every
≤250 ms plus per button (`firmware/zectrix/main/vibe_note4_ui.cc:303, 331`).
Each controller state change also copies the whole struct in `publish_view`
(`app_controller.c:113-147`). Both UIs then gate actual rendering on a
revision/hash, so most of these copies render nothing.

- **Change options, least invasive first:**
  1. Under the mutex, compare `revision`; if unchanged since the caller's last
     copy, skip the struct copy entirely (clock ticks already bump `revision`
     via `update_clock_fields`, so staleness is preserved).
  2. Longer term, split hot fields (clock, busy, battery) from cold payloads
     (usage snapshots, todo list) into separately versioned sub-structs.
- **User benefit:** removes a recurring ~5 KB mutex-guarded copy every
  100-250 ms on both boards. The absolute CPU cost is small on a 160/240 MHz
  part — treat the win as hygiene plus reduced mutex hold time (UI task wakes
  with the controller worker waiting on the same mutex), **[hypothesis]** until
  profiled.
- **Tradeoffs:** option 1 is a localized, low-risk change that keeps the
  existing threading contract (display code only ever touches a copied view;
  callbacks stay non-blocking). Option 2 touches the public header used by
  both boards and the host tests — larger blast radius.
- **Validation:** existing ASan/UBSan host tests
  (`tests/host/test_app_smart_lifecycle.c`) still pass; add a unit test that
  counts copies across a sequence of publishes/wakes; optional on-device
  profile via the existing heap/stack telemetry log in `publish_view`.

## 4. Batch the per-day NVS cache writes during reconcile — small–medium effort

**[measured]** `vibe_reconcile_one` calls `mark_cache_dirty()` +
`write_cache_now()` after **each** day it fetches
(`components/vibe_usage/src/vibe_client.c:714-716`), writing the full
≤4 KiB cache blob up to 7 times per reconciliation, paced at one day/second
by the controller (`app_controller.c:791, 810-812`).

- **Change:** persist once after the batch (or on reconcile completion /
  controller-idle), while still persisting the reconcile *pending mask*
  early so a crash knows which days are incomplete. The day-candidate
  transactionality rule (only complete, schema-valid responses replace a day)
  is untouched.
- **User benefit:** ~6 fewer 4 KiB NVS blob writes per reconcile cycle →
  less flash wear on the 24 KiB `nvs` partition and a slightly faster
  background reconcile.
- **Tradeoffs:** a power loss mid-reconcile re-fetches unpersisted days on
  next boot (cheap, rate-limited). Must keep storage failures visible
  (an existing hard requirement) and keep Last-Known-Good semantics intact.
- **Validation:** extend the reconcile tests in `tests/host/test_core.c`
  (crash between days, storage-failure visibility, tombstone generation on
  401) and run the full `./tools/validate.sh --static` suite.

## 5. Voice scratch: use PSRAM on Note4; keep flash on Passport — medium effort

**[measured]** Every voice capture erases the entire `voicebuf` partition
(128 KiB) before recording: `esp_partition_erase_region(voice, 0, voice->size)`
at `components/smart_todo/smart_todo_llm.c:167`. `RECORD_BYTES` is
`8000 Hz × 8 s × 2 B = 128,000` bytes (`smart_todo_llm.c:16-17`) against a
131,072-byte partition — so *reducing the erase range* saves only ~3 KiB and is
**[rejected]** as an optimization by itself. The real cost is that a
supposedly cheap interaction (hold-to-talk) performs a whole-partition erase
plus streaming flash writes every time.

- **Change:** on Note4 (8 MB octal PSRAM, `CONFIG_SPIRAM_USE_MALLOC=y`),
  record into a PSRAM buffer and drop the flash round-trip entirely — zero
  erase wear and lower capture latency. On Passport (no PSRAM, internal RAM
  too tight for a 128 KB buffer) keep the flash path, made board-selectable
  behind the existing `board_audio`/`smart_todo` boundary.
- **User benefit:** eliminated flash wear per voice command and a snappier
  recording start on Note4; benefit size **[hypothesis]** until timed on
  hardware.
- **Tradeoffs:** two code paths where there is one; must keep the existing
  silence/veto check (`smart_todo_llm.c:198`) and the WAV header assembly
  untouched. `smart_todo` is not inside `vibe_usage`, so no layering change is
  needed.
- **Validation:** smart-todo host tests (`tools/test-smart-todo.sh`), then a
  device test recording on Note4 with erase/write counters observed via logs.

## 6. Factor out the duplicated per-board UI page/menu model — large effort

**[measured]** `firmware/ai-passport/main/vibe_passport_ui.c` (1,218 lines)
and `firmware/zectrix/main/vibe_note4_ui.cc` (1,098 lines) independently
implement the same logical UI: the same 11-entry settings menu (Passport
`render_settings` at :415 vs. Note4 `HandleSettingsSelection` at :472-529),
footer arrows (:259 vs. :1092), boot hold detection (:1142-1151 vs.
:283-298), alert redirect (:1097-1109 vs. :306-320), and 60 s battery
sampling (:1117-1122 vs. :317-321).

- **Change:** extract the *decision* layer — menu structure, navigation
  state machine, settings enumeration — into a shared component, leaving all
  *rendering* (LVGL vs. 1-bpp EPD canvas) under each `firmware/*` target, the
  same pattern already used by the shared `epd_refresh_policy` component.
  This respects the boundary "board UI stays under the matching firmware
  target".
- **User benefit:** every cross-board feature (new language, new settings
  entry, alert behavior change) is implemented once; behavior can no longer
  drift between the Passport and Note4 builds.
- **Tradeoffs:** the largest refactor on this list; touches the code with the
  strictest pixel/font regression requirements. Do it incrementally, starting
  with the settings menu enumeration.
- **Validation:** the full gate — `./tools/validate.sh` (catalog, canvas,
  LVGL pixel tests, config migration tests, both firmware builds), plus LVGL
  pixel/font regressions called out in `AGENTS.md`.

## 7. Add streaming-parser fuzzing and host coverage to CI — small effort

**[measured]** `components/vibe_usage/src/vibe_parser.c` (646 lines) is a
hand-rolled streaming JSON parser consuming untrusted network data, exercised
today by fixture-based host tests under ASan/UBSan
(`tools/test-host.sh`). The repo has **no** coverage measurement and **no**
fuzzing anywhere (`tools/validate.sh`, `.github/workflows/ci.yml`).

- **Change:** a libFuzzer/clang target around `vibe_parser` in `tests/host`
  plus a coverage run (e.g. `gcovr`) of the existing host suite as a CI
  artifact. Optionally raise stack-budget checks from compile-time-only to a
  host-executed assertion.
- **User benefit:** defects in the most internet-exposed code surface before
  a device ever sees them; coverage numbers quantify every other
  optimization's regression risk.
- **Tradeoffs:** some CI minutes; fuzzer needs the parser's ceiling constants
  (2 MiB body / 20,000 buckets / depth 16) kept low in fuzz builds to stay
  fast.
- **Validation:** self-evident (these are tests); fuzz runs clean for a fixed
  corpus/time budget in CI.

## 8. Passport idle strategy beyond backlight dimming — investigate before building

**[measured]** The Passport build has no deep sleep; its only idle behavior is
backlight dimming to `brightness/5` after 30 s of inactivity
(`vibe_passport_ui.c:1123-1129`) with Wi-Fi remaining connected. Note4
already has a full shutdown path (`vibe_note4_ui.cc:531-553`).

- **Direction:** an idle timeline (dim → display off → modem-sleep pairing
  with recommendation 2 → timed wakeup via the RTC timer) is the standard
  path for an always-display device on a battery. However, `AGENTS.md`
  explicitly forbids *unverified deep-sleep scheduling*, so this must be
  **measure-first**: prototype behind a flag, characterize current on
  hardware, and only then propose a default.
- **User benefit:** battery life — entirely **[hypothesis]** until power
  evidence exists. Do not ship or claim anything before that.
- **Validation:** the power-test evidence format from `AGENTS.md` (board,
  power source, exact action, expected vs. observed, sanitized evidence)
  plus the preserved 5 s UP boot hook.

## 9. Micro-fixes (take or leave)

- **[measured]** Passport forces a re-render every 60 s battery sample even
  when SoC/mV are unchanged (`ui->force_render = true` at
  `vibe_passport_ui.c:1121`); set it only when a displayed value actually
  changed. Trivial; saves one full-screen LVGL render per minute.
- **[measured]** `vibe_aggregate.c:39,51` does a `strcmp` walk over the
  source list for every matched bucket — bounded (≤20,000 buckets × ≤12
  sources) but the hottest inner loop; a tiny index table would remove it.
  Low priority; only worth it alongside other `vibe_usage` work.
- **[measured]** Note4 leaves >12 MB of flash unused after `voicebuf`; a
  future core-dump partition (`CONFIG_ESP_COREDUMP_TO_FLASH`) would give
  post-mortem crash evidence on the field device. Sizing and enabling is a
  separate decision; no partition changes happen implicitly.

## Investigated and rejected

- **Shrinking the `voicebuf` erase range:** `RECORD_BYTES` (128,000) already
  covers ~98% of the partition (131,072); savings ≈ 3 KiB per erase. Useless
  as a standalone change — see recommendation 5 for the real fix.
- **Touching `esp_wifi_connect` internals for performance:** vendored and
  intentionally frozen per `docs/upstream-lock.md`; any delta must stay
  minimal and auditable. Recommendation 2 deliberately works through the
  existing public API only.
- **Switching the Note4 button task from polling to GPIO interrupts:**
  the polling pattern lives in the locked zectrix board layer
  (`zectrix_board.cc:247,295`); churn there is not justified without power
  evidence that it matters.

---

## Validation status of this deliverable

```text
Build: NOT RUN (analysis only; no source changed)
Host tests: NOT RUN (no code changed; existing suites unaffected)
CI: NOT RUN (no commits to firmware paths; will run on this docs commit)
Real API: NOT RUN
Device tests: NOT RUN (no hardware flashed, per task scope)
Installer tests: NOT RUN
Power tests: NOT RUN (all power claims above are explicitly labeled hypotheses)
Unverified: every projected benefit — binary-size delta (#1), current draw
  (#2, #8), copy-time saving (#3), erase-latency saving (#5) — requires the
  validation named in each item before any PASS claim.
```
