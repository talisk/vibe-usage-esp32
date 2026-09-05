# Shared-core synchronization policy

Both boards are intentionally built from the same repository and the same
`components/vibe_usage`, `app_controller`, `device_config`, and `wifi_adapter`
source. There is no generated second copy to synchronize.

## Compatibility contract

The following identifiers evolve independently and must never be changed as an
unrecorded refactor:

| Contract | Current value | Required action when changed |
| --- | --- | --- |
| public component API | project version `0.0.1` (root VERSION) | SemVer review and both targets rebuilt |
| cache encoding | `VIBE_CACHE_SCHEMA_VERSION = 2` | migration or explicit safe invalidation tests |
| displayed metric | `API_TOTAL_V1` | new metric ID, old-cache invalidation, API revalidation |
| auth blob | `VUA1` / `auth_v1` | tombstone and rollback tests |
| settings blob | `VUF1` / `settings_v1` | migration and corrupt-NVS behavior tests |

Board-specific code may format, paginate, or refresh the same snapshot
differently. It may not reinterpret totals, parse a second API shape, accept a
different trust origin, or persist a private cache schema.

## Change checklist

For any shared-core change:

1. add or update a host test that proves the behavior without real credentials;
2. run ASan/UBSan host tests and Python layout tests;
3. clean-build both ESP32-C3 and ESP32-S3 targets from the pinned lock files;
4. enforce stack-frame and 3 MiB image budgets;
5. repeat hardware cases whose boundary changed and record each board
   separately;
6. update the API contract or ADR when wire or data semantics changed.

Do not accept “works on the S3” as evidence for a parser or memory change: the
Passport has no PSRAM and is the shared-core resource floor. Conversely, a C3
build does not prove NOTE4 EPD base/shadow, RTC, or latch behavior.

If the core is later extracted as an external component, use a pinned release
and lock hash in both firmware targets. The extraction is complete only after
the same host fixtures and both device smoke tests pass against the external
package; do not maintain drifting source copies.
