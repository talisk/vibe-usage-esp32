---
name: flash-firmware
description: Build, verify and safely flash Vibe Passport or Vibe Note firmware from this repository when the user asks to install, update, burn or flash a connected device (烧录固件、一键刷机). Also supports a no-write dry run.
---

# Flash Vibe firmware

Complete a requested firmware update with one user prompt, using the existing
build/verifier/installer workflow. Do not treat invoking or editing this skill
as permission to flash unless the user's request includes installation.

## Establish the target

Work from the repository root (three directories above this skill folder).
Read `AGENTS.md`, `docs/installation.md` and `docs/upstream-lock.md` completely;
consult the latest `docs/acceptance/` record for prior evidence, not remembered
port assignments. Preserve unrelated changes and the root `VERSION`.

| User product | Build selector | Required hardware |
| --- | --- | --- |
| Vibe Passport | `passport` | FoloToy AI Passport, ESP32-C3, 8 MB, no PSRAM |
| Vibe Note | `note4` | ZECTRIX NOTE4 BLACK-WHITE, ESP32-S3, 16 MB, 8 MB octal PSRAM |

NOTE4C is unsupported. USB VID/PID identifies an adapter, not the board model.
If the intended product or physical target is ambiguous, ask the user to identify
it or disconnect unrelated boards before probing. Never select the first port
or apply an old port name blindly. For both devices, handle each target separately.

## Build and verify

Activate the user's installed ESP-IDF 5.5.3 environment; locate its `export.sh`
from the current environment or user configuration, not a developer-specific path.
If it is missing, stop and explain the prerequisite rather than installing or
changing the toolchain without permission. Confirm `idf.py --version`.

For a source update, run `./tools/validate.sh --static`, then
`./tools/build.sh passport` or `./tools/build.sh note4` (use `all` for both).
If this task changes firmware, also run the full `./tools/validate.sh` gate.
Builds verify their merged images, partition MD5, chip/flash constraints and
protected boundaries. Do not flash on any validation failure or silently reuse
an older binary. For a user-selected release, follow the release checksum and
layout procedure in `docs/installation.md` instead of replacing it with a build.

## Flash only the requested device

Immediately before each write, freshly enumerate ports with
`python3 -m serial.tools.list_ports`. Resolve the exact target with the user if
needed. Announce the selected product/port and that saved settings are retained.
Set `VIBE_BOARD` to `passport` or `note4`, `VIBE_PORT` to the freshly resolved
port, and `VIBE_BUILD` to its absolute verified build directory. Then inspect:

```bash
./tools/flash.sh "$VIBE_BOARD" "$VIBE_PORT" "$VIBE_BUILD" --dry-run
```

A dry run checks the image without opening the serial port. If the user asked
only for a preview or diagnosis, stop here. An explicit request to flash is
sufficient authorization; do not require a redundant confirmation when the
target is unambiguous. For that authorized update, run:

```bash
./tools/flash.sh "$VIBE_BOARD" "$VIBE_PORT" "$VIBE_BUILD" --yes
```

Use this installer, not a parallel raw esptool write path. It reads chip and
flash identities before writing and verifies every written segment. Only
bootloader 0x0, partition table 0x8000 and application 0x10000 may be written.
Never erase the whole chip/NVS, touch eFuses, export or write Passport cardid
0x356000+0x4000 or Recovery 0x700000+0x100000, or add settings/data segments.
Preserve the Passport five-second UP Recovery hook.

If enumeration, identity, transport or verification fails, stop that device's
write attempt and report whether writing started. Request reconnection or the
vendor-documented download mode, then rediscover ports before retrying. Do not
erase, force a mismatch, kill an unrelated serial monitor or retry indefinitely.

## Verify and hand off

Require successful segment hashes and installer exit status before reporting
flash success. Record board, chip/flash, root version, app checksum and write
result. Observe boot for a bounded interval (for example 45 seconds); opening
serial can reset the board. Keep only sanitized version, restored-state,
TLS/HTTP status and panic markers; do not retain raw logs, credentials, hardware
addresses, network names or personal Usage responses.

Report Build, Host tests, CI, Real API, Device, Installer, Power and Unverified
separately. A successful write is not screen/button/phone-scan acceptance, and
HTTP 401 is not an authenticated API success. Leave physical verification to
the user when it cannot be observed directly. Never publish or commit as part
of flashing unless separately requested.
