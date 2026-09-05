# Installation, recovery, and safe flashing

Read this page completely before connecting a board. The two targets are not
interchangeable, and a generic full-flash erase is unsafe for AI Passport.

> **Never run an `erase-flash` or whole-chip erase command.** Passport contains
> factory identity at `0x356000+0x4000` and permanent Recovery at
> `0x700000+0x100000`. This project neither reads nor writes those regions.

## Supported boards

| Selection | Required hardware | Expected identity |
| --- | --- | --- |
| `passport` | FoloToy AI Passport | ESP32-C3, 8 MB flash, no PSRAM |
| `note4` | ZECTRIX NOTE4 BLACK-WHITE 400 × 300 | ESP32-S3, 16 MB flash, 8 MB octal PSRAM |

NOTE4C is a different product and is unsupported. Disconnect other ESP devices
while installing so a port cannot be selected by accident.

## Build and verify from source

Install ESP-IDF 5.5.3, activate its environment, and run from the repository
root:

```bash
./tools/validate.sh --static
./tools/build.sh all
```

The result is under `build/firmware/passport` and
`build/firmware/note4`. Each build contains three writable segments, a merged
image, and `flash-plan.json`. `verify_firmware.py` checks the target chip, flash
size, exact partition table and MD5, 3 MiB factory boundary, bootloader magic,
Passport Recovery hook, and the end of the merged image. A Passport merged file
is rejected if it reaches the first protected byte—even if those bytes contain
`0xff`, because writing them could still erase a sector.

Inspect without opening a serial port:

```bash
./tools/flash.sh passport /dev/cu.EXAMPLE --dry-run
./tools/flash.sh note4 /dev/cu.EXAMPLE --dry-run
```

## Identify the connected device

Every write must begin with a fresh port inventory. On macOS:

```bash
ls -1 /dev/cu.usb* /dev/cu.SLAB* 2>/dev/null
```

Do not infer the board from a remembered port name. The installer performs
read-only `chip_id` and `flash_id` queries and refuses a target or flash-size
mismatch. To run those checks manually after setting the exact port:

```bash
VIBE_PORT=/dev/cu.EXAMPLE
python3 -m esptool --chip esp32c3 --port "${VIBE_PORT}" chip_id
python3 -m esptool --chip esp32c3 --port "${VIBE_PORT}" flash_id
```

Use `esp32s3` for NOTE4. If the board does not enumerate, stop: reconnect a
known data-capable cable, try the board's documented download-mode sequence,
and inventory ports again. Do not compensate with an erase.

## Write a verified source build

The following command is the only project-provided write path. The final
`--yes` authorizes that exact board, port, and verified build directory:

```bash
./tools/flash.sh passport /dev/cu.EXAMPLE build/firmware/passport --yes
./tools/flash.sh note4 /dev/cu.EXAMPLE build/firmware/note4 --yes
```

The script writes only:

| Offset | Segment |
| --- | --- |
| `0x0` | project bootloader |
| `0x8000` | verified partition table |
| `0x10000` | factory application |

It never emits a whole-chip erase and never writes Passport card identity or
Recovery. Do not add an NVS segment: preserving NVS is what makes a normal
firmware update keep Wi-Fi, account, and cache state.

## Install a release package

Download the release archive and its `SHA256SUMS`, keep its directory layout,
then verify every file from the extracted release directory:

```bash
shasum -a 256 -c SHA256SUMS
```

Read the matching `flash-plan.json` and identify the board as above. A release
contains both segmented files and a merged image. Segmented writes are preferred
for auditability. From the extracted release directory, use the bundled verified
installer with its board directory:

```bash
./tools/flash.sh passport /dev/cu.EXAMPLE passport --dry-run
./tools/flash.sh note4 /dev/cu.EXAMPLE note4-black-white --dry-run
```

Replace `--dry-run` with `--yes` only after checking the exact port. The script
cross-checks `flasher_args.json`, `flash-plan.json`, image magic, partition MD5,
merged image content, app boundary, and Passport protected-region boundary
before opening the port. If another installer accepts only the merged image, it must write
the matching board file at offset `0x0`, must not request an erase, and must not
pad or expand that file. Never use the Passport image on NOTE4 or vice versa.

## First boot

With no saved Wi-Fi, the screen shows a randomized setup SSID and
`http://192.168.4.1`. Connect a phone, enter a 2.4 GHz network, then return the
phone to an internet connection and approve the Device Flow QR. The portal is
an open local AP in P0; use it only while physically present. It must never ask
for a Vibe password or API key.

Application serial logs may contain device state and error categories, but
suppress network identifiers, assigned addresses, Device Flow URLs/codes,
Wi-Fi passwords, Authorization values, raw personal Usage responses, and
factory card identity. The chip ROM and esptool can print a hardware address
during identity checks; redact it before retaining or sharing a transcript.

## Recovery and reset

AI Passport's permanent Recovery application is factory-owned. Hold UP for five
seconds during boot to invoke the preserved bootloader hook. This project does
not redistribute or replace the Recovery image. Use the vendor's documented
Recovery path if the factory application no longer starts.

The in-app factory reset deletes only project-owned Vibe/config namespaces and
saved Wi-Fi credentials. It writes a reset journal first and resumes an
interrupted reset on the next boot. It does not erase the NVS partition as a
whole. Account unlink hides the credential and usage locally; because P0 NVS is
plaintext and flash is wear-leveled, also revoke access at the service before
losing, selling, or transferring a device.

For NOTE4, DOWN hold for three seconds requests a clean network stop, clears the
panel, shuts down peripherals, and releases the battery latch. USB power may
keep some rails alive; that behavior is a hardware test, not an installation
guarantee.

## Troubleshooting boundaries

- A successful build proves compilation and layout only.
- `chip_id`/`flash_id` prove the connected silicon and flash response, not the UI.
- A successful write proves transport and checksum verification, not Wi-Fi,
  Device Flow, Usage accuracy, RTC wake, or battery behavior.
- A 401 proves that the route and auth gate responded; it is not a valid-account
  end-to-end success.

Record each evidence class separately in the latest file under the source
tree's `docs/acceptance/` directory.
