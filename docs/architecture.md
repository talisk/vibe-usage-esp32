# Architecture and invariants

This document describes the P0 implementation shared by the two firmware
targets. The frozen wire contract is in [`api-contract.md`](api-contract.md),
and hardware-specific constraints are under [`upstream/`](upstream/).

## System boundary

The device is a direct HTTPS client. A phone is used only for the local Wi-Fi
portal and the browser approval step; it is not a proxy for Usage data.

```text
buttons -> board UI -> app_controller -> wifi_adapter -> Wi-Fi / SNTP
                              |
                              +-> vibe_usage -> Device Flow / Usage HTTPS
                                      |
                                      +-> streaming parser -> day candidate
                                      +-> aggregate/cache -> NVS A/B blobs
```

There is one product lifecycle owner. `app_controller` consumes button intents,
Wi-Fi events, timers, and shutdown requests through a FreeRTOS queue. Network,
NVS, time synchronization, and account transitions execute serially on its
worker task. Display callbacks only read a copied `app_controller_view_t`; they
do not perform HTTP or block on Wi-Fi.

## Component responsibilities

| Component | Owns | Must not own |
| --- | --- | --- |
| `vibe_usage` | dates, Device Flow, HTTPS policy, streaming parser, exact aggregation, auth/cache storage | display, GPIO, Wi-Fi portal |
| `app_controller` | lifecycle, serialized side effects, deadlines, cancellation, retry/backoff | panel pixels or board power rails |
| `wifi_adapter` | stable C boundary around station and portal behavior | Vibe credentials or Usage data |
| `esp_wifi_connect` | up to ten saved networks, scan/selection, captive portal | account authorization |
| `device_config` | timezone, refresh interval, random display identifier, config revision, language, alert volume | auth or daily usage |
| `vibe_ui` | four-language catalog, bounded UTF-8 decoding and font subsets | network, NVS, GPIO |
| `firmware/ai-passport` | LVGL screens, BSP lock, buttons, backlight | shared protocol rules |
| `firmware/zectrix` | 1-bpp canvas, EPD refresh, RTC, latch, buttons | shared protocol rules |

## Lifecycle and data state

Language changes are serialized controller intents. Persist `vibe_cfg/language_v1`
as a byte before publishing the new language to either UI and the next captive
portal session. The existing CRC-protected `settings_v1` blob is unchanged, so
upgrading does not regenerate device IDs or invalidate usage/auth caches. An
absent, invalid, or wrong-type language key falls back to English; other storage
errors propagate. A failed save leaves the selected language unchanged and
shows a Settings footer error. Note forces a full EPD refresh when language changes.

Alert volume uses the independent `vibe_cfg/alert_vol_v1` byte. Settings cycles
through mute, 25, 50, 75, and 100 percent; a successful non-muted change plays a
preview chime. The default is 50 percent. The original `settings_v1` blob remains
binary-compatible.

Lifecycle and data freshness are intentionally independent. The lifecycle moves
through configuration, Wi-Fi, time, link, synchronization, dashboard, settings,
reset, and shutdown states. The selected dataset is separately `READY`, `EMPTY`,
`STALE`, `AUTH_REQUIRED`, or `LINK_REQUIRED`. A network error can therefore keep
the dashboard visible while marking its last known data stale.

Snapshot state is scoped to the requested window: Today uses today's slot;
7D requires all seven slots before it can be Ready or Empty. An empty Today
never makes a nonempty complete 7D snapshot Empty. Status exposes both windows.

Every account has a monotonically increasing generation. Every timezone change
increments `config_revision`. Async results are accepted only for the active
generation and configuration. Cancellation invalidates in-flight work before a
relink, timezone change, Wi-Fi reconfiguration, reset, or shutdown.

## Device Flow and trust boundary

The bootstrap origin is fixed to `https://vibecafe.ai`. Device Flow returns a
browser URL, user code, device code, API credential, and optionally an API
origin. An origin is accepted only when it has the required HTTPS scheme,
trusted host, and no user-info, query, fragment, or non-default port. Redirects
are disabled. The Bearer value is attached only to the accepted Usage origin
and never crosses into UI state.

Polling honors the server interval, `slow_down`, expiry, denial, and one-time
delivery semantics. Wall-clock validity is required for TLS and date ranges;
Device Flow expiry itself uses the monotonic clock.

## Usage transaction

One request covers exactly one local calendar day using explicit UTC instants
from local midnight through the final millisecond of that day. The API's `to`
boundary is inclusive. The response is parsed incrementally with these ceilings:

- 2 MiB body, 20,000 buckets, nesting depth 16;
- exact non-negative decimal `uint64_t` parsing, without a `double` round-trip;
- required and unique `source`, `bucketStart`, and `totalTokens` per bucket;
- every bucket timestamp must map to the requested day;
- unknown fields are syntax-checked and skipped;
- pagination-like metadata fails closed until implemented.

Parsing writes to a temporary day candidate. Only a complete HTTP 200 response
that passes every boundary and checked addition replaces the cached day. A
truncated body, transport failure, schema error, overflow, or out-of-day bucket
leaves the previous Last Known Good value untouched. A legitimate complete
empty day is different from an error and is stored as zero.

Sources are aggregated into 24 internal slots. The public snapshot exposes at
most 11 sources plus `__other__` (12 rows total), sorted deterministically, with
basis points derived from integer totals. Display abbreviations never enter
storage or tests.

## Rolling cache and storage

The cache keeps `[today-7, today]`; the visible seven-day total uses
`[today-6, today]`. The eighth slot supports rollover and recovery. A CRC-covered
binary blob includes schema, sequence, generation, configuration revision,
metric, timezone, source dictionary, daily slots, and reconciliation progress.
The encoded blob is capped at 4096 bytes.

Cache schema 2 rejects earlier date-only query results, which could omit local
morning buckets. Authentication and device configuration retain schema 1, so
upgrading preserves account and Wi-Fi credentials while rebuilding usage data.

NVS ownership is deliberately narrow:

| Namespace | Keys | Purpose |
| --- | --- | --- |
| `vibe_auth` | `auth_v1` | CRC-covered credential or higher-generation tombstone |
| `vibe_cache` | `cache_a`, `cache_b` | alternating snapshots; highest valid sequence wins |
| `vibe_meta` | `retry_v1`, `reset_v1` | persisted rate-limit deadline and reset journal |
| `vibe_cfg` | `settings_v1`, `language_v1`, `alert_vol_v1` | product settings and independent UI preferences |
| `vibe_llm` | `settings_v1`, `asr_lang_v1` | voice endpoints, models, credentials, upload mode, and ASR language |
| `wifi` | vendored component keys | saved Wi-Fi networks |

A 401 first persists a higher-generation auth tombstone, then hides current RAM
data and clears the old cache. If persistence fails, RAM is still invalidated
and the storage failure remains visible. Factory reset journals intent before
erasing only the owned namespaces and Wi-Fi credentials. Boot resumes an
interrupted local reset before loading settings. Whole-NVS erase is forbidden.

## Retry and failure policy

HTTP operations have bounded connect/read/total time, body limits, TLS
verification, and cancellation. DNS, TLS, network, HTTP status, time, schema,
and storage failures remain distinguishable. Transient failures use exponential
backoff with jitter, capped at 30 minutes. HTTP 429 persists the later of local
backoff and the bounded `Retry-After` deadline so a reboot cannot create a retry
storm. Manual refresh is rate-limited to one attempt per minute.

## Board-specific behavior

Passport uses the BSP's LVGL lock for every UI mutation and dims its backlight
after 30 seconds of inactivity. Its build and installer must preserve the fixed
factory-owned identity and permanent Recovery regions plus the five-second UP
boot hook.

NOTE4 renders a 400 × 300 1-bpp frame. Layout changes, QR transitions, or ten
partial updates force a full refresh. A partial update is allowed only after a
successful base frame; the shadow advances only after driver success. DOWN hold
requests controller shutdown, clears the EPD, disables peripherals, and releases
GPIO17. The retained physical image is not treated as a recoverable RAM base
after reboot.

## Explicit P0 exclusions

P0 does not claim custom OTA, Secure Boot, Flash Encryption, eFuse provisioning,
encrypted local Wi-Fi setup, arbitrary timezones, or validated RTC-alarm battery
cycling. These require separate installation and hardware evidence.

## Smart TODO extension (2026-09-06 candidate)

The current candidate also owns `vibe_todo` and `vibe_llm` NVS namespaces.
Controller intents connect the TODO page, bounded audio transcription and
configured Chat Completions requests. Timers play local ES8311 reminder chimes.
LLM credentials remain separate from Vibe authentication and UI snapshots.

The Settings LLM portal is a temporary station-LAN server with a session QR.
The existing SoftAP portal still owns Wi-Fi setup. NOTE4 programs NFC NDEF for
that setup hotspot; Passport's passive tag requires a one-time phone write.
The old P0 audio/NFC exclusions describe the baseline, not this extension.
See [ADR 0002](adr/0002-voice-todo-and-configured-llm.md) and the
[development handoff](smart-todo-development.zh_CN.md) for complete contracts.
