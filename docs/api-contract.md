# Vibe Usage API contract

Observed on 2026-09-05 against the configured HTTPS Vibe Usage service, using
an existing local account credential. The probe was read-only. Credentials,
source names, project names, hostnames, response bodies, and real token totals
were neither printed nor stored.

## Frozen P0 request contract

- Bootstrap origin: `https://vibecafe.ai` (the credential may return the same
  trusted origin as `apiUrl`).
- Metric: `API_TOTAL_V1`, the checked sum of each bucket's `totalTokens`.
- Daily range: explicit UTC ISO-8601 instants for local midnight through the
  last millisecond before the next local midnight, with `tz=Asia%2FShanghai`
  (or `UTC`). For Shanghai September 3, this is
  `from=2026-09-02T16:00:00.000Z&to=2026-09-03T15:59:59.999Z`.
- The API includes a bucket exactly at `to`. A nonempty boundary probe found
  that next-midnight `to` included the following day's midnight bucket; naked
  dates also omitted an early-morning bucket. The initial exclusive-boundary
  inference from sparse samples was incorrect and is superseded here.
- Cache schema 2 invalidates date-only cache entries without changing auth or
  Wi-Fi storage; affected days are fetched again with the corrected bounds.
- `days=1` did not map to the selected local calendar day during the probe, so
  firmware daily reads do not use it.
- Response root fields observed: `buckets`, `sessions`, `hasAnyData`.
- No pagination, cursor, truncation, or `next` field was present in the sampled
  responses. This is an observation, not proof that a large response can never
  be paginated. A future such field must fail closed until its semantics are
  implemented.
- A complete empty daily range returned `buckets=[]` while `hasAnyData=true`.
  Therefore `hasAnyData` describes broader account history and is not used to
  infer whether the requested day has usage.
- Every sampled required field had the expected type. Every sampled token
  integer was within the exact JSON integer range of the probing client.
- For every sampled bucket, `totalTokens` matched
  `inputTokens + outputTokens + reasoningOutputTokens`. It did not match the
  desktop app's `computedTotal` when cached input was present. The device never
  silently changes between these metrics.

## Required response shape

```json
{
  "buckets": [
    {
      "source": "codex",
      "bucketStart": "2026-09-04T16:00:00.000Z",
      "totalTokens": 9007199254740993
    }
  ],
  "sessions": [],
  "hasAnyData": true
}
```

Unknown fields are syntax-checked and skipped. Each bucket must contain exactly
one string `source`, one absolute ISO-8601 `bucketStart`, and one non-negative
decimal integer `totalTokens`. The parser rejects partial bodies, duplicate
required fields, out-of-day buckets, unsafe syntax, overflow, excessive depth,
more than 20,000 buckets, or more than 2 MiB of response data. A rejected
candidate never replaces Last Known Good data.

## Device Flow

The request and response field names follow the public Vibe Usage macOS client
at commit `34d50754e0504b4a33b87d1f7927d462f30cb98e`. A real device-flow approval
is part of hardware acceptance and is intentionally not emulated by embedding a
desktop API key in firmware. Device Flow responses are bounded to 8 KiB,
require a JSON content type and complete transfer, reject raw or JSON-escaped
embedded NUL values before C-string decoding, reject duplicate protocol fields,
and must parse exactly to the received-body boundary. An optional field that is
missing or explicitly `null` is absent, matching the pinned Swift decoder; a
non-null `apiUrl` must be a string that normalizes to the trusted bootstrap
origin. Delivered keys must use the pinned `vbu_`
prefix and URL-safe alphanumeric, underscore, or hyphen characters so they
cannot introduce HTTP-header control bytes.
