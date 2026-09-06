# LLM configuration and local setup portal

The two firmware targets share `components/llm_portal`. It stores the voice
TODO configuration and serves its local management page. It does not start
an LLM request from an HTTP callback. `app_controller` starts/stops the page
and loads the latest saved settings before the next voice operation.

## Phone setup

1. Open **Settings → LLM configuration** on Vibe Passport or Vibe Note.
2. If the device is offline, use the existing setup hotspot QR code. Join
   `VibePassport-XXXX` or `VibeNote-XXXX`, then use its captive portal at
   `http://192.168.4.1` to connect the device to a 2.4 GHz Wi-Fi network.
3. Join that same home/office Wi-Fi network on the phone. After the device
   has an IP address, its LLM page displays a new QR code. Scan this second
   QR code to open `http://192.168.x.x/?token=...` in the phone browser.
   The actual station address can also be another private IPv4 range.
4. Check **Enable voice TODO**, enter the Chat and ASR settings, and press
   **Save**. The page confirms only after NVS persistence succeeds.
5. Return from the configuration page on the device. On the TODO page,
   hold OK, wait for its recording indication/beep, speak, and release OK.
   The next command uses the settings you saved.

Wi-Fi setup and LLM setup are separate stages. The existing captive portal
owns port 80 during SoftAP setup. The LLM server may start on port 80 only
after station connection and after that captive server stops. The controller
closes LLM access when leaving its page, losing Wi-Fi, starting Wi-Fi setup,
shutting down, or reaching the ten-minute configuration deadline. Reopen
the device's LLM page and scan again if the browser reports an expired session.

NFC provisioning is an additional way to reach the Wi-Fi setup flow; it does
not expose the LLM session token or API keys. Hardware NFC behavior and the
Passport passive-tag requirement are documented with the board integration.

## Endpoint fields

| Field | Default | Meaning |
| --- | --- | --- |
| Enable voice TODO | Off | Allows a physical voice request to contact the configured services |
| Chat Endpoint | `https://api.openai.com/v1/chat/completions` | Full OpenAI Chat Completions-compatible endpoint |
| Chat Model | `gpt-4o-mini` | Model accepted by the selected Chat service |
| Chat API Key | Empty | Used only as that Chat request's Bearer credential |
| ASR Endpoint | `https://api.openai.com/v1/audio/transcriptions` | Full OpenAI audio transcription-compatible endpoint |
| ASR Model | `gpt-4o-mini-transcribe` | Model accepted by the selected transcription service |
| ASR Language | `zh` | Optional provider language code; leave empty for provider auto-detection |
| ASR Upload | `Auto` | Provider-aware transport, or an explicit multipart/Base64 override |
| ASR API Key | Empty | Independent Bearer credential for the audio service |

The OpenAI, OpenRouter, SiliconFlow China, and SiliconFlow Global presets fill both endpoint URLs and
recommended model names. Model availability and access still depend on the
configured provider/account. Selecting a preset resets ASR Language to `zh` and
ASR Upload to `Auto`. The language value accepts letters, digits, hyphen, and
underscore, for example `zh`, `en`, or `zh-CN`; an empty value omits the field.
In Auto mode OpenRouter receives a JSON request containing a Base64 WAV;
OpenAI, SiliconFlow, Codex gateway, and custom endpoints receive multipart WAV.
Choose `multipart/form-data WAV` or `Base64 WAV in JSON` to override that choice
for any provider or custom compatible service.

SiliconFlow China uses `https://api.siliconflow.cn/v1`; SiliconFlow Global uses
`https://api.siliconflow.com/v1`. Both presets fill the Chat Completions and
Audio Transcriptions routes, `Qwen/Qwen3-8B`, and
`FunAudioLLM/SenseVoiceSmall`. Choose the region where the API key was created.
The preset is a convenience baseline: verify current model availability and
account access in that region before physical acceptance.

The device does not append `/v1` or a route to either endpoint field. Chat must
support JSON `messages`, non-streaming responses, `max_tokens`, and a single
`choices[0].message.content` string with `finish_reason: "stop"`. Multipart ASR
receives `model`, `response_format=json`, an optional `language`, and a mono PCM16
WAV file. Base64 ASR receives JSON with `model`, `response_format=json`, the same
optional `language`, and `audio` containing the
same WAV encoded as Base64. The firmware records at 8 kHz for at most eight
seconds. Recording starts locally while OK is held; releasing OK finalizes one
WAV, uploads it once to ASR, and starts Chat only after ASR succeeds.

For a local server, a supported example is
`http://192.168.1.10:8000/v1/chat/completions`. Its ASR endpoint must also be
configured; a text-only Chat server cannot transcribe microphone audio. Leave
the respective key empty for a service that intentionally needs no key.
For the same cloud account, enter the same key into both fields if both APIs
use it. An empty ASR key does not silently borrow the Chat key.

Saved keys are never returned by the management API or displayed in the page.
For an unchanged endpoint, an empty key input preserves the previous key. The
**Clear saved key** checkbox explicitly removes it; a new value plus a checked
clear box is rejected. In the browser, changing an endpoint with a blank key
clears that endpoint's saved key on Save. Switching connection presets discards
unsaved keys and requires replacement or explicit clearing before Save; it does
not silently forward a saved cloud key to a gateway.
**Reset LLM settings** removes only the LLM configuration and both LLM keys,
restores disabled defaults, and leaves TODO items, Wi-Fi, and the Vibe account
untouched. The device's full factory reset also clears LLM settings.

## Transport and storage boundaries

### Codex / ChatGPT subscriptions

Choose **Codex subscription (experimental host gateway)** in the configuration
page. Enter the host's private LAN base URL, such as `http://192.168.1.10:8765`,
and its dedicated gateway key. The page fills both endpoints and the local
`codex-default` / `codex-voice` aliases. Saving an unchanged gateway with both
keys already present permits a blank key to preserve them. A different gateway
requires its key again. The preset is inferred from the saved endpoint/model
pair and adds no NVS fields.

Run the [host gateway](../services/codex_gateway/README.zh_CN.md) on an online
macOS/Linux computer, NAS, or Raspberry Pi with the documented Codex CLI version
and a ChatGPT subscription login. `codex login --device-auth` can complete the
initial login with a browser authorization code, but the host must continue
running the gateway afterward. Both
LLM and ASR use that login through different service paths: Codex tasks for LLM,
experimental Voice WebRTC v3 for ASR. Voice and Codex task quotas are separate.
The device stores only the gateway token; account login and refresh remain with
the official host client. Subscription credentials cannot replace a Platform API
key directly. See the [source investigation and limitations](codex-subscription-feasibility.zh_CN.md)
and [current acceptance evidence](acceptance/2026-09-06-codex-subscription.md).

### URL policy and local storage

`llm_endpoint_valid()` is shared by the store and outgoing client. URLs are
bounded to 255 bytes and require a nonempty path. HTTPS accepts a syntactically
valid multi-label DNS name or a public IPv4 address. HTTP accepts only literal
RFC1918 addresses: `10.0.0.0/8`, `172.16.0.0/12`, and `192.168.0.0/16`.
An explicit port from 1 to 65535 is accepted. Credentials in URLs, queries,
fragments, control characters, backslashes, malformed escapes, local hostnames,
IPv6 literals, and noncanonical numeric IP forms are rejected. This is a URL
policy, not a DNS resolver; it does not prove the public routing of a DNS name.
The actual HTTPS client attaches the ESP certificate bundle and disables redirects.

The local management page intentionally uses HTTP on the current Wi-Fi network.
Use a trusted network while entering keys. Anyone holding the current device
QR link can manage this configuration during the active session; this is
physical-session access, not encrypted administration. Keys are stored in the
device's normal unencrypted NVS. No eFuses, Flash Encryption, or Secure Boot are
enabled by this feature. Recordings go to the selected ASR provider; the resulting
transcript and current TODO titles go to the selected Chat provider only when
the user starts a voice command.

The server creates a new 128-bit random token for each start. Page requests need
the exact token query, and API requests require `X-LLM-Token`. The server checks
the current IP address, the literal-IP `Host` header, and any supplied `Origin`.
It provides no CORS permission. Responses use `no-store`, `no-referrer`, a restrictive
CSP, `nosniff`, and frame blocking. A token is not logged or written to NVS/NFC.

Settings use `vibe_llm/settings_v1`, a schema-tagged, CRC-protected blob, plus
the independent `asr_lang_v1` string. Existing installs without that key migrate
to `zh`; saving an empty string preserves provider auto-detection.
Absent settings return disabled defaults without writing to flash. Corrupt data
or NVS errors fail the load and clear its output; no key may be used after a
failed load. Updates use NVS set/commit and propagate all storage failures.
This feature does not change the existing `vibe_cfg/settings_v1` representation,
Vibe account origin rules, or factory-owned flash partitions.

## Local management API

| Request | Auth | Result |
| --- | --- | --- |
| `GET /?token=<current-token>` | Query token + Host/Origin | Embedded configuration page |
| `GET /api/settings` | `X-LLM-Token` + Host/Origin | Endpoints/models/enabled plus `api_key_set`/`asr_key_set`; no secrets |
| `POST /api/settings` | Same, `Content-Type: application/json` | Validates and commits configuration, returns the same redacted representation |
| `DELETE /api/settings` | Same, empty body | Removes LLM settings and returns disabled defaults |

POST requires `enabled`, `chat_url`, `chat_model`, `asr_url`, and `asr_model`.
Optional fields are `api_key`, `asr_key`, `clear_api_key`, and `clear_asr_key`.
At this raw API layer, an omitted or empty key preserves the saved key even if
the endpoint changes; callers must send the matching `clear_*` field explicitly.
The browser implements the stricter endpoint-switch behavior described above.
Unknown or duplicate fields, wrong types, escaped/embedded NUL, and nested
containers fail closed. The request is limited to 4096 bytes. The HTTP server
has two client slots, a 6144-byte task stack, and five-second socket read/write
timeouts. Body reads additionally stop at a ten-second absolute deadline
(plus the currently blocked socket read). Error responses close the connection
so ESP-IDF cannot drain an arbitrarily large rejected body. It performs only local validation/storage; cloud work stays on the
serialized controller task.

## Verification and acceptance

Run the component checks and actual client protocol mocks from the repository:

```bash
components/llm_portal/tests/run.sh
tools/test-smart-llm.sh
```

Both run with AddressSanitizer and UndefinedBehaviorSanitizer. The portal tests
compile the actual endpoint, NVS store, and HTTP handler sources. They cover
valid/private URL policy, rejected origin forms, defaults/round-trip/corruption,
storage failures, token rotation, Host/Origin rejection, key redaction and
preserve/clear behavior, fragmented request input, and malformed/oversized JSON.
The client tests compile `smart_todo_llm.c` against HTTP/audio boundary mocks;
they check exact WAV and multipart bytes, partial reads/writes, release padding,
silence rejection, TLS/redirect configuration, malformed/truncated/oversized
responses, HTTP errors, cancellation, and cJSON allocation failures. Audio I/O
uses a 200 ms timeout during capture, restoring five seconds for response reads.
This bounds individual transport waits; physical release latency under a slow
network still requires measurement on each board.

These host results do not prove microphone wiring, speaker volume, station
reachability from a phone, TLS success against a live provider, actual ASR quality,
or reminder timing on physical boards. For each target's acceptance, record the
firmware revision/hash, confirm the two QR stages, save/reboot/reload, test blank
key preservation and explicit clear, expire/reopen the portal, and make a real
voice add/complete/delete command with the intended service. Also test a local
HTTP server, invalid/expired credentials, network loss during upload, and device
release/shutdown while the server stops reading audio. Keep real keys and voice
recordings out of logs and acceptance artifacts.

## References

- [ESP-IDF 5.5.3 HTTP server and URI handler lifecycle](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32/api-reference/protocols/esp_http_server.html)
- [OpenAI Chat API reference](https://developers.openai.com/api/reference/resources/chat)
- [OpenAI transcription API reference](https://developers.openai.com/api/reference/resources/audio/subresources/transcriptions/methods/create)
- [Project architecture](architecture.md) and [existing Wi-Fi setup](../README.md#connect-and-link-your-account)
