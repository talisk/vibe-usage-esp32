# ADR 0002: Voice TODO, configured model services and NFC setup

Status: implemented candidate, physical acceptance pending.

The product adds a local TODO board and user-selected model services. The
existing Vibe account API keeps its fixed-origin Device Flow policy. LLM
credentials and destinations are separate, explicitly configured by the owner.
This ADR does not relax the Usage credential policy.

## Decisions

- Keep the controller as the sole serial network/lifecycle owner. Button
  callbacks enqueue intents; OK release and cancellation use atomic flags so
  release is observed while the worker is uploading. No HTTP, NVS or display
  refresh runs in a button callback.
- Use OpenAI-compatible `POST /v1/audio/transcriptions` for ASR, followed by
  `POST /v1/chat/completions` for interpretation. Both full endpoint paths,
  models and credentials are independently configurable. Chat-only providers
  require a separate compatible ASR service. There is no hidden proxy or
  bundled cloud credential.
- Upload 8 kHz mono PCM16 WAV in small blocks. The upload has an eight-second
  fixed WAV length; release stops capture and pads the remaining samples with
  silence. Audio is not stored in NVS or a flash partition. This keeps the
  no-PSRAM Passport usable without a whole-recording allocation.
- Interpret a single explicit instruction into `add`, `complete`, `delete` or
  `noop`. A complete/delete operation may contain multiple item IDs; duplicate
  or nonexistent IDs are ignored while valid IDs still apply. Validate exact schema, types and bounds locally. Model
  output never directly edits storage. Completed items remain visible until
  explicitly deleted. After deletion, renumber the displayed items from 1 in
  their current order so spoken numbers continue to match the visible sequence.
- Store at most 12 items, each with a 96-byte UTF-8 title. Use a versioned,
  CRC-protected NVS blob and publish the candidate only after successful save.
  A corrupt or inaccessible store disables mutations and reports the failure.
  No empty-list fallback overwrites the failed store.
- Persist UTC deadlines and fixed recurrence intervals. Reminders work while
  powered, including offline after valid time was established; overdue alarms
  are delivered after reboot once time is valid. Explicit power-off has no
  promised RTC wake path. Missed recurring periods coalesce into one chime.
- Open the configuration HTTP server only from Settings, on the station LAN,
  for ten minutes. A fresh random token in the screen QR authenticates the
  session. Host, Origin and header-token checks protect mutations; saved keys
  are never returned to the browser. Leaving the page, timeout, Wi-Fi changes
  and shutdown close the server.
- LLM HTTPS uses the certificate bundle and disables redirects. Explicit local
  HTTP endpoints must use an RFC1918 IPv4 literal. Reject credentials in URLs,
  control characters, query strings and fragments. There is no TLS bypass.
- NOTE4 can write NDEF through I2C; program an open setup-hotspot WSC credential
  record and a portal URI. Clear stale records on boot and leaving setup.
  Passport has a passive NTAG213 without an MCU write interface: prepare its
  stable hotspot/URI record once with a phone. It cannot follow changing LAN
  IPs or configuration-session tokens. QR remains the portable phone path.

## Costs and acceptance limits

The dynamic TODO title fonts cover the GB2312/Big5/JIS X 0208 union; they are
separate from the small UI catalogs. Keep the original 3 MiB application and
protected Passport partition boundaries. The clean build is the size gate.
Unsupported glyphs use the renderer fallback, not arbitrary per-title glyph
recentering or hidden cloud image rendering.

A fixed eight-second upload wastes some bandwidth for a short utterance but
avoids buffering it on C3. Capture/network errors discard the request. Mic
release, codec gain, speech accuracy, TLS heap margin and audible reminders
still require actual boards and a configured service.

The ASR service receives the recording. The Chat service receives the transcript
and existing TODO titles/IDs. Neither receives the Vibe account credential or
Usage history. Local settings and TODO storage follow the existing unencrypted
NVS baseline; no eFuse/security/partition migration is introduced.
