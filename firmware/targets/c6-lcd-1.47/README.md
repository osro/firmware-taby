# Experimental Taby: Waveshare ESP32-C6 Touch LCD 1.47

USB-first development port for **ESP32-C6-Touch-LCD-1.47** and **-M** (pin-header
variant). This is separate from the official ESP32-S3 release bundles.
See [recorded verification](../../../STATUS.md) for the tested hardware,
outcomes and remaining limits. [Waveshare board documentation](https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47)
defines its JD9853 LCD, AXS5106L touch controller and pinout.

## Behavior

The 172×320 LCD shows the original Taby artwork resized proportionally to
172×280, centred with black margins. The port boots through `startup` into
`idle_01_loop`. One-shot clips return to idle; looping clips repeat, including
their wraparound. `confirmation>idle_01_loop` plays a two-clip sequence.

Sixteen clips are currently included: startup, idle, listening, prerecorded
talking, working, searching, confirmation, busy, task completed, task created,
disappointed, sleeping, building, waiting, Codex and Claude. Exact IDs, durations,
source hashes and frame counts are generated in `dist/c6-lcd-1.47/assets.json`.
The original semantic command resolver is reused. Missing clips return
`unsupported_animation` without changing playback.

USB speaks newline-terminated ASCII at 115200 baud. Supported commands include
`PING`, `INFO`, `DIAG`, `ANIMATIONS`, `CLEAR`, `BRIGHTNESS?`, `BRIGHTNESS 70`, raw
animation IDs, `CMD <command>`, upstream semantic animation commands,
`TOUCH_SIGNAL` and `GESTURE_SIGNAL`. USB transport selection is accepted;
other transport modes are rejected. A short touch increments the tap counter;
a hold reports `hold_start` and `hold_end`. Touch is reported to the controller
and does not switch clips by itself.

This version provides USB animation control and touch. Wi-Fi, Bluetooth, account
provisioning, live Rive lip-sync, reusable UI cards/timers, automatic rotation,
and persistent settings are outside this milestone. It identifies itself as
`c6-lcd-1.47` / `0.1.0-c6-dev.1` and advertises only implemented capabilities.
Desktop Taby app integration has **not** been verified. No replacement MCP
server is included.

## Build

Use Python 3.12 and ESP-IDF **5.5.2**, following Espressif's official installation
instructions. The prebuilt-install `.venv` and ESP-IDF's tool environment are
separate. From the repository root:

```sh
python3.12 tools/bootstrap.py
.venv/bin/python -m pip install -r tools/requirements-c6.txt
.venv/bin/python tools/c6_assets.py
.venv/bin/python tools/check.py
.venv/bin/python -m unittest discover -s tests -v
```

In an ESP-IDF 5.5.2 shell, from the repository root:

```sh
idf.py -C firmware/targets/c6-lcd-1.47 \
  -B "$PWD/firmware/build-c6-lcd-1.47" \
  -D "SDKCONFIG=$PWD/firmware/sdkconfig.c6-lcd-1.47" build
```

Keep build outputs and the generated `sdkconfig` outside `firmware/targets` so
the original S3 source-fingerprint tooling cannot accidentally include them.
The build has no external managed components: the attributed JD9853 driver is
vendored locally, and portable protocol/state sources are reused from the
shared firmware. The asset converter requires Pillow 12.0.0; its image-specific
unit tests explicitly skip when that optional C6 dependency is absent.

Animation frames are converted offline to a 64-color RGB565 palette and bounded
indexed RLE. At runtime one 96,320-byte DMA-capable framebuffer suffices. The
3,821,341-byte pack is mapped read-only from flash and checked against a generated
CRC32 at boot. Metadata also records its SHA-256. Frame data is checked before
decoding. A display-transfer fault latches playback off until reset so a timed-out
DMA transfer cannot race a buffer rewrite; USB diagnostics remain available.

## Install and verify

Establish the **exact board** and confirm 8 MB flash before installing. This
development target changes the partition layout and replaces the existing
application. Back up the current board first, using this repository's `.venv`:

```sh
.venv/bin/python -m esptool --chip esp32c6 --port PORT flash_id
.venv/bin/python -m esptool --chip esp32c6 --port PORT \
  read_flash 0 0x800000 original-esp32-c6-8mb.bin
```

Keep the backup private and outside Git; it may contain device-specific data.
Confirm its size is 8,388,608 bytes and record its SHA-256. Review the build's
`flasher_args.json` against these regions, then install the four images together:

```sh
.venv/bin/python -m esptool --chip esp32c6 --port PORT --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 firmware/build-c6-lcd-1.47/bootloader/bootloader.bin \
  0x8000 firmware/build-c6-lcd-1.47/partition_table/partition-table.bin \
  0x10000 firmware/build-c6-lcd-1.47/taby_c6.bin \
  0x200000 dist/c6-lcd-1.47/assets.bin
```

`PORT` is the established board's serial port. Do not use `--force`, erase the
whole chip, modify eFuses, or use the official S3 installer on this target.
The four writes do not cover `0x9000..0xffff`, but old vendor settings may be
incompatible with the new layout; the full backup is the restoration path.

```sh
.venv/bin/python tools/c6_verify.py --port PORT
```

The hardware check holds one serial connection and waits through a complete
cycle of every clip. It checks frame progress, wraparound/completion, stable
heap, missing clips, long-line recovery, sequence advancement, USB transport
acknowledgements and touch-controller reads. Tap and hold during the test to
record real input. Separately inspect the actual screen at normal size.

On the tested Mac, opening a serial connection can reset this C6 through native
USB control-line transitions. Use one persistent connection for normal control
and verification; the startup animation may replay when a tool opens the port.

## Restore the owner's original firmware

The tested board's private backup is retained in
`.local/backups/original-esp32-c6-8mb-20261117.bin`, with a checksum file beside it.
These files are intentionally ignored and are **not** part of any downloadable
bundle. After checking that checksum and reconnecting the **same board**:

```sh
.venv/bin/python -m esptool --chip esp32c6 --port PORT --baud 460800 \
  write_flash --flash_mode keep --flash_freq keep --flash_size keep \
  0 .local/backups/original-esp32-c6-8mb-20261117.bin
```

This restores the saved application and data. It does not erase or change eFuses.
Use Waveshare's documented BOOT/RESET recovery if the port cannot be opened.
Preserved licenses and vendor provenance are listed in [NOTICE](NOTICE).
