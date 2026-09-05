# ADR 0001: Parse Usage responses as a bounded stream

- Status: accepted
- Date: 2026-09-05
- Scope: both firmware targets

## Context

The Passport is an ESP32-C3 without PSRAM. A Usage response can contain many
buckets, and JSON libraries commonly materialize the whole body plus a DOM.
Their numeric representation can also round integers above `2^53`. The product
must preserve exact unsigned 64-bit totals and must never replace valid cached
data with a partial or malformed response.

## Decision

The production Usage path uses the incremental parser in
`components/vibe_usage/src/vibe_parser.c`. It consumes arbitrary HTTP chunks,
keeps bounded token state, parses `totalTokens` from decimal characters into a
checked `uint64_t`, and aggregates into a temporary day candidate. The candidate
is committed only after end-of-body validation.

The parser enforces body, bucket, depth, token, timestamp, day-range, duplicate
field, and arithmetic limits. Unknown fields remain forward-compatible but are
fully syntax-checked. Any pagination-like field fails closed because silently
showing page one as a complete day is worse than showing stale data.

cJSON remains acceptable for the small, explicitly bounded Device Flow control
responses. It is not used for the Usage data plane.

## Consequences

- Peak memory scales with fixed parser and aggregate state, not response size.
- Integers larger than JavaScript's exact range remain exact.
- The code has more states than a DOM traversal and therefore needs chunk-boundary,
  UTF-8, malformed-input, overflow, and truncation tests.
- Supporting new pagination semantics requires an explicit protocol change; an
  unknown response will remain Last Known Good instead of being undercounted.
