# Smart TODO candidate — 2026-09-06

This is a development acceptance record, separate from the earlier production
branding/Usage API evidence. Source starts at `b8109a0` on `main` with the local
Smart TODO changes; there is no new commit, release, deployment or device write.

## Scope and evidence

| Requirement | Implementation | Evidence required for acceptance |
| --- | --- | --- |
| TODO page on both boards | Both UIs, 12-item stable-ID model, paging/completed display | Host rendering plus actual panel/buttons |
| Hold OK voice add/complete/delete | Atomic release, ES8311, multipart ASR, validated Chat action and NVS commit | Mock protocol + actual microphones/services/storage |
| LLM Settings and Wi-Fi/LAN QR | Existing Wi-Fi portal then temporary authenticated LAN settings server | HTTP handler tests, QR decode + phone end-to-end |
| OpenAI compatible endpoint management | Full Chat/ASR URLs, model/key/enable, independent credentials | Endpoint/storage/HTTP tests + owner's actual service |
| Relative and periodic reminders | Persisted UTC deadline, fixed repeat, local chime, retry and overdue handling | Core failure/reboot tests + real speaker/time/offline runs |
| NFC setup on both hardware types | NOTE4 dynamic WSC+URI; Passport once-written passive WSC+URI | Byte-level NDEF tests + first phone write and actual RF joins |
| Documentation | User/developer/config/hardware guides, ADR, provenance | Link/static checks and requirement audit |

## Current boundary

No USB serial device was available during initial inspection on this run.
No firmware was flashed and no LLM credential was supplied or read from an
unrelated source. No real microphone, speaker, NFC, battery, installation,
Recovery or configured model-service result is claimed by host tests.

A Passport NTAG213 is passive and not wired for MCU writes. Its NFC branch
requires one initial phone write using the device's actual persistent setup
SSID. This is a necessary hardware setup step, not an automated firmware
feature. NOTE4 can program the records over I2C. Phone OS handling of WSC and
multi-record NDEF remains a physical compatibility gate; QR is also available.

The firmware reminds while powered. Explicit NOTE4 power-off does not add RTC
wake/latch scheduling. A missed reminder is handled after reboot once the clock
is valid. Fixed recurrence is supported; weekday/calendar schedules are not.

## Physical acceptance procedure

1. Build the final source and record the application/merged SHA-256 values.
   Re-discover ports, read the chip and flash facts, then use the existing
   segmented installer or the Passport Recovery package under its flash skill.
   Do not erase flash or read/write protected identity/Recovery partitions.
2. Boot each board on USB; inspect version, heap/largest block/stack metadata,
   original saved account and Usage history. Confirm the ordinary pages still
   work. A clean boot is not a microphone test.
3. Open Settings → LLM config before and after Wi-Fi has been configured.
   Follow the hotspot QR, open `192.168.4.1`, save the router, switch the phone
   to the router LAN, and scan the final token URL. Verify the form saves and
   reads non-secret settings. Confirm keys do not appear in responses/logs.
4. Configure a real compatible ASR and Chat endpoint/model/key. Return to TODO.
   Hold OK, wait for Listening/chime, say “新增散步，两小时后提醒我”, release.
   Require exactly one item, deadline about now+7200, no extra action.
5. Say “完成散步”, then “删除散步”. Check completion removes its alarm; deletion
   affects only the chosen item. Add two identically named items and give an
   ambiguous command: it must leave both unchanged or request clarification.
6. Add “喝水，一分钟后提醒，每分钟重复”. Hear the real speaker at each due time;
   confirm the same ID produces a new alert even after the screen dimmed.
   Complete it and wait another interval: it must not sound again.
7. Disconnect Wi-Fi while idle after time sync: the local alarm still works.
   Disconnect during capture/inference: no partial operation commits. Release
   OK during slow EPD refresh and weak network: record measured stop latency.
8. Restart after creating reminders. Verify list/IDs/deadlines survive; for a
   missed recurring item hear one catch-up chime, not one per missed interval.
   Check NVS-full/save errors remain visible without replacing the list.
9. NOTE4: enter setup, touch Android and iPhone, record whether each joins the
   AP or opens the URI. Exit/timeout, read the tag to confirm safe URI and no
   previous setup record remains. Repeat with RF field present during cleanup.
10. Passport: write its open Wi-Fi record and fixed URI once using the phone
    instructions. Confirm NDEF fits 144 user bytes and no lock/protection is
    enabled. Re-enter setup and test touch-to-join on the actual phone. iOS may
    require scanning the Wi-Fi QR before a URI tap. Repeat after ordinary reboot.
11. Test Note explicit shutdown during setup/inference. The worker must stop
    network/audio/NFC work and ignore queued Wi-Fi/intent events; no station
    restart while the panel clears and the power latch releases.
12. Repeat at least 20 voice commands and page transitions on Passport, with
    maximum list length and Chinese titles. Record minimum internal heap,
    largest block, controller stack, errors and audio/display state. Do not use
    a single idle heap observation as peak-memory evidence.

Attach sanitized observations only. Keep user audio, titles and credentials out
of committed logs or screenshots. This checklist remains open until the actual
matching observations are recorded.

## Verified local candidate

The final `./tools/validate.sh` completed with exit 0 under ESP-IDF 5.5.3.
It ran all host/repository checks followed by isolated clean builds of both
boards and actual LVGL/Note-canvas tests. The earlier incremental checks are
not substituted for this final clean gate.

| Category | Status | Scope |
| --- | --- | --- |
| Build | PASS | Both clean targets, original 3 MiB app limits, merged layouts and protected ranges |
| Host tests | PASS | Core/schema/storage, streaming ASR/Chat, portal handlers, controller lifecycle, NDEF, UI |
| CI | NOT RUN | No remote workflow triggered; local actionlint unavailable |
| Real API | NOT RUN | ASR/Chat tests use explicit mock network/audio boundaries |
| Device tests | NOT RUN | USB serial inventory empty at the final recheck |
| Installer tests | NOT RUN | Packaging/layout verifier passed; no device write/Recovery session |
| Power tests | NOT RUN | No battery, latch, current or physical power-loss run |

Local artifacts are in `build/verified/`. `smart-todo-candidate.json` records
the base commit, a digest of the firmware sources, every source-file digest,
image digests and the validation-log digest. The separate
`build/verification-logs/smart-todo-validation.log` retains the local gate output. These files are ignored build outputs, not committed
release assets. The candidate still reports the repository VERSION 0.0.1;
identify this development build by the following checksums.

| Board | Application bytes | Application SHA-256 | Merged SHA-256 |
| --- | ---: | --- | --- |
| Vibe Passport | 2835808 | `9d92061175d053c8f088dabaf620e9dca3187b873e493ca7a21b14de070713b1` | `66a3eb81744ea507fde79693c65030ebf0c8519e430a5ab6951a0fee6ff3cd73` |
| Vibe Note | 2902848 | `2d556171889519f5f04d84a6e5a225aecd56bbe66348dd760e06754b06d2c3ba` | `cd24606acbed0ada126a3ac69147195deceafcc3f2ed2b5d91d294329aa18ac3` |

New stack-frame gate results: `handle_voice` 176 B, `smart_reminders` 32 B,
`smart_todo_transcribe` 1200 B and `smart_todo_interpret` 96 B (maximum across
the two target builds). These are compiler single-function frame sizes, not
a measurement of total call-chain stack or peak runtime heap.

The host tests execute production C/C++ sources at explicitly stubbed platform
boundaries. They include failed allocation/transport/storage cases, complete
and partial HTTP bodies, strict JSON structures, release/padding, shutdown
stale-event handling, repeated alert sequence numbers and physical-button
state checks. Board audio additionally rejects RX queue overflow so a stalled
upload cannot silently turn truncated speech into an action. Overflow ISR,
codec behavior and button timing still need hardware verification.

The controller does not start new Usage network operations within 40 seconds
of a pending reminder, and voice-start cancels current Usage work. This limits
interference; it is not an experimentally verified end-to-end timing bound.

Final artifact privacy scan passed for both merged images and the candidate
manifest (3 files). The manifest was rechecked against every recorded firmware
source file and both delivered image digests. The local build log is retained
separately because it contains developer build paths and is not a release asset.

## Continuation: commit-boundary feedback and diagnostics

The first candidate above remains historical evidence. A continuation found
that a cancellation arriving after a successful NVS save could replace the
visible success message with “Voice cancelled”. The current source checks
cancellation immediately before save and preserves the successful result once
the candidate is committed. It also emits fixed numeric phase/HTTP/error/heap
metadata for real-device diagnosis, without private text or credentials.

Production-controller host regressions reproduce both sides of the cancellation
boundary, distinguish ASR 401 and Chat 429 from storage failures, and assert
that endpoint/key/title/transcript canaries never enter formatted logs.
`heap_min_internal` is the allocator's since-boot low-water value, not an
isolated per-voice peak. See the development guide's diagnostic field table.

The continuation again found no USB serial devices. The device/service/NFC
acceptance gates remain open; no new device write or real API call occurred.

### Current candidate after continuation

This table supersedes the earlier checksum table for the files currently in
`build/verified/`. The updated source passed the full `./tools/validate.sh`
with exit 0: all host checks, both isolated clean builds, image/protected-layout
checks, stack budgets and real LVGL/canvas regressions. The matching log is
`build/verification-logs/smart-todo-telemetry-validation.log`.

| Board | Application bytes | Application SHA-256 | Merged SHA-256 |
| --- | ---: | --- | --- |
| Vibe Passport | 2836336 | `8738d9ddb8ff7771f13d5d9ef4b50815a5b14400339db4f7167800592658ea67` | `4e259cf7c29449056886ec4c29db37af7bb2e265bffd665dda124b8e932d2e7c` |
| Vibe Note | 2903344 | `4f5afd3e05c7d69989dbbd3c23efff7f7598cdf9637ef2cb5eb9201d350b6b3a` | `2c63da5270120fb262df88384fc3305827567e2a423b3d37bc682e388e19e6cc` |

Stack frame budgets: PASS (decode_cache_into=80B, fetch_today=32B, handle_voice=224B, poll_device_link=48B, reconcile_one=32B, smart_reminders=32B, smart_todo_interpret=96B, smart_todo_transcribe=1200B, vibe_cache_decode=32B)

Build and Host tests remain PASS. CI, Real API, Device, Installer and Power
remain NOT RUN; those boundaries have not changed with the added diagnostics.

## 订阅配置后续构建

本记录中的旧候选 checksum 只对应当时构建。Codex subscription 配置已新增，
`build/verified/` 的同名 merged image 已被新构建替换。当前源码、固件 hash、
主机合成 ASR/LLM 实测和未验收边界以
[订阅接入验收](2026-09-06-codex-subscription.md)及
`build/verified/codex-subscription-candidate.json` 为准。原清单已标记 superseded。
