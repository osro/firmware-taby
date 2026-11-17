# Experimental Taby: Waveshare ESP32-C6 Touch LCD 1.47

Development port for **ESP32-C6-Touch-LCD-1.47** and **-M** (pin-header
variant). This is separate from the official ESP32-S3 release bundles.
See [recorded verification](../../../STATUS.md) for the tested hardware,
outcomes and remaining limits. [Waveshare board documentation](https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47)
defines its JD9853 LCD, AXS5106L touch controller and pinout.

## Version status

Fork source `0.2.0-c6-dev.6` incorporates upstream 1.4.0 and asset catalog
0.4.1. It is a development build, not the currently installed/physically
verified dev.5 build. See STATUS for each build’s evidence.

Use the matching generated `0.2.1-sd` full pack with dev.6. Upstream changed
one animation, so the full pack checksum/catalog differ from the dev.5 card’s
`0.2.0-sd` pack. The 16-clip flash fallback is byte-for-byte unchanged. The
verified dev.5 binary/source/SD packages are preserved locally; preparation
has not updated the connected board or its card.

## Behavior

The 172×320 LCD shows the original Taby artwork fitted upright with centered
black margins. Storage frames are172×280; display fitting is described below. The port boots through `startup` into
`idle_01_loop`. One-shot clips return to idle; looping clips repeat, including
their wraparound. `confirmation>idle_01_loop` plays a two-clip sequence.

Sixteen clips are currently included: startup, idle, listening, prerecorded
talking, working, searching, confirmation, busy, task completed, task created,
disappointed, sleeping, building, waiting, Codex and Claude. Exact IDs, durations,
source hashes and frame counts are generated in `dist/c6-lcd-1.47/assets.json`.
The original semantic command resolver is reused. Missing clips return
`unsupported_animation` without changing playback.

The expanded `0.2.0-c6-dev.6` firmware adds saved brightness, eye motion
(`normal`, `calm`, `still`), four screen rotations and sensor-based automatic
rotation. Packed sideways artwork is normalized upright before fitting the screen.
The face uses280×172 on a landscape screen and172×106 on a portrait screen,
with centered black margins and unchanged aspect ratio. Voltage is calibrated, but battery percentage remains unknown.

USB uses newline-terminated commands at 115200 baud. In addition to animation
IDs and upstream semantic commands, supported commands include:

- `PING`, `INFO`, `DIAG`, `IMU`, `ANIMATIONS`, `CLEAR`, `STATE`, `POWER`.
  `IMU` is a bounded read-only diagnostic with cached sensor samples/errors,
  acceleration in mg and actual configuration-register readback.
- `BRIGHTNESS?`, `BRIGHTNESS 70`, `EYE_MOTION still`,
  `DISPLAY_ORIENTATION 90`, `DISPLAY_ORIENTATION auto`.
- `TRANSPORT_DEFAULT usb|wifi|ble` saves the boot preference;
  `TRANSPORT_MODE usb|wifi|ble` activates a transport. USB stays available.
  Once started, wireless services stay active until restart.
- `TOUCH_SIGNAL`, `GESTURE_SIGNAL`, `CHOICE_SIGNAL`: short taps, holds and
  two-choice selections are separate counters/signals. Touch does not switch
  animations by itself.
- All ten basic upstream card kinds: for example
  `UI/text:REMINDER|Take a break`, `UI/timer:FOCUS|65|300|Keep going|run`,
  `UI/progress:UPLOADING|45|Almost halfway`, and
  `UI/choice_2:CHOOSE|YES|NO|Touch a button`.
- Title/text card decoration, for example
  `UI/title&shooting_stars/4:WELL DONE` or
  `UI/text&heart_rain/4:THANK YOU|You did it`. All 24 upstream decor names
  retain their eight family mappings, with adapted small-screen line artwork.
  Effects use eight small objects, stop on time and leave the card visible.
- `VOICE_MOTION <talk> <clockMs> <atMs> <frames>` and
  `VOICE_MOTION <talk> stop`: upstream 30fps motion data drives simplified
  raster mouth shapes. Other successful presentations cancel speech.

Wi-Fi provides local HTTP control and a protected setup access point. Use
`SETUP_START` then `SETUP_INFO` over USB to read the temporary network name,
password and setup address. Join it from a phone, open `http://192.168.4.1/setup`
and enter the desired 2.4GHz network credentials. Never publish this response:
it contains the setup password. `PROVISION` also accepts upstream credential
JSON over USB. Up to five network profiles persist; `WIFI_NETWORKS`,
`WIFI_NEARBY`, `WIFI_PREFER <SSID>` and `WIFI_FORGET <SSID>` manage them.
Ordinary INFO/profile listings omit passwords. Setup credentials change after
restart. HTTP routes include `/ping`, `/state`, `/touch`, `/v1/health`,
`/v1/reusable/choice-signal`, `/cmd?c=<URL-encoded-command>`, `/setup`, and
`POST /provision`. Commands use plain HTTP on the local network.

Bluetooth advertises `Taby-<device suffix>`, with upstream service UUID
`54414259-0000-4000-a000-000000000001`; command/event/state/power characteristic
suffixes are `0002` through `0005`. Subscribe to the event characteristic for
completed commands; a successful write means queued. State/power reads are
cached. Desktop protocol 2 adds raw readable choice JSON on `0006` and a
three-field local identity JSON on `0007`, using the same genuine device ID
as USB. USB capability `bluetooth_v2` enables the installed app’s pairing flow.
Orientation events use its native `OK DISPLAY_ORIENTATION ...` format.
Commands are at most 255 bytes; long event replies are readable up to
511 bytes, and oversized notifications return `ERR mtu`.

Optional FAT32 microSD storage supplies all 84 animations from the separate
`--sd-card` package. Copy its `taby/` directory to the card root, insert the
card with the board powered off, then restart. Firmware checks the complete
pack size/CRC before playback; allow roughly half a minute for a full-library
startup check before USB becomes ready. Missing/bad media falls back to the sixteen
onboard clips. It does not format cards. After a card read failure, restart
to remount it. See [STATUS](../../../STATUS.md) for actual physical-card tests.

This remains a custom development target. Desktop 1.0.13 recognises USB and
its GUI pairs over Bluetooth, with successful display/rotation controls. The
Bluetooth client disables eye-mode controls and shows Assets Unknown; use USB
for those settings/metadata. App-restart reconnection was confirmed by the owner. MCP
access is not configured and official cloud enrollment is unavailable. No factory identity is fabricated.
Embedded card clip behaviors remain rejected. Voice gesture/body and
expression/eye pose artwork is not animated; this is not the Rive renderer.
Dev.4 corrects a sensor self-test setup error; its four-way text-card direction
was physically verified. Dev.5 also aligns the packed face artwork; the owner
and device confirmed upright face in all four positions. See STATUS for evidence.
Battery operation still needs owner feedback.

## Build

Use Python 3.12 and ESP-IDF **5.5.2**, following Espressif's official installation
instructions. The prebuilt-install `.venv` and ESP-IDF's tool environment are
separate. From the repository root:

```sh
python3.12 tools/bootstrap.py
.venv/bin/python -m pip install -r tools/requirements-c6.txt
.venv/bin/python tools/c6_assets.py
.venv/bin/python tools/c6_assets.py --sd-card
.venv/bin/python tools/check.py
.venv/bin/python -m unittest discover -s tests -v
```

In an ESP-IDF 5.5.2 shell, from the repository root:

```sh
python tools/c6_build.py
```

This wrapper also records the build fingerprint required by
`tools/c6_package.py`. Package with
`.venv/bin/python tools/c6_package.py --output dist/c6-development`, using a
fresh output directory. See the [root setup guide](../../../README.md#c6-setup).

Keep build outputs and the generated `sdkconfig` outside `firmware/targets` so
the original S3 source-fingerprint tooling cannot accidentally include them.
The attributed JD9853 driver is vendored locally. Managed LVGL 8.3.11 and
mDNS 1.10.1 are pinned; portable protocol/state/UI parsers and the unchanged
voice parser are reused from shared firmware. The asset converter requires Pillow 12.0.0; its image-specific
unit tests explicitly skip when that optional C6 dependency is absent.

Animation frames are converted offline to a 64-color RGB565 palette and bounded
indexed RLE. At runtime one 96,320-byte animation framebuffer is reused for voice;
small private DMA strips serve animation and LVGL transfers. The
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
mkdir -p .local/backups
.venv/bin/python -m esptool --chip esp32c6 --port PORT \
  read_flash 0 0x800000 .local/backups/original-esp32-c6-8mb.bin
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
.venv/bin/python tools/c6_controls_verify.py --port PORT
.venv/bin/python tools/c6_voice_verify.py --port PORT
```

The hardware check holds one serial connection and waits through a complete
cycle of every clip. It checks frame progress, wraparound/completion, stable
heap, missing clips, long-line recovery, sequence advancement, USB transport
acknowledgements and touch-controller reads. Tap and hold during the test to
record real input. Separately inspect the actual screen at normal size.

The C6 verification tools assert both USB control lines before opening the
port; this avoids the processor reset observed when clearing them on this Mac.
Other serial apps may still reset the processor while leaving the card powered,
which can make this card time out during initialization. If that happens,
unplug USB until the screen is dark and reconnect it. Prefer one persistent
connection. Explicit hardware-reset/profile-persistence checks may require a
subsequent full power cycle before SD playback.

Bluetooth verification additionally requires `pip install bleak==3.1.0` in the
repository virtual environment and macOS Bluetooth access. Run
`tools/c6_network_verify.py --port PORT --skip-profile-restart` to preserve an
already initialized SD card. Omit that option only when a subsequent power
cycle is available; the profile test uses and removes a generated test SSID
only if the existing profile list is empty.

## Restore your original firmware

Use the full flash backup you made from your own board before installation.
The example path below is ignored by Git and is **not** part of any downloadable
bundle; a new checkout does not include a backup. After checking the saved
checksum and reconnecting the **same board**:

```sh
.venv/bin/python -m esptool --chip esp32c6 --port PORT --baud 460800 \
  write_flash --flash_mode keep --flash_freq keep --flash_size keep \
  0 .local/backups/original-esp32-c6-8mb.bin
```

This restores the saved application and data. It does not erase or change eFuses.
Use Waveshare's documented BOOT/RESET recovery if the port cannot be opened.
Preserved licenses and vendor provenance are listed in [NOTICE](NOTICE).


To exercise the installed macOS app's own native Bluetooth bridge, close Taby
first, then run `tools/c6_native_bluetooth_verify.py --port PORT`. Add
`--touch-seconds 90` for prompted LEFT/RIGHT hardware checks. This uses the
existing app's bridge; it does not modify its bundle or personal data. Keep
opaque native scan identifiers exactly as returned; do not normalize their case.

With the desktop app closed, use `.venv/bin/python tools/c6_decor_verify.py --port PORT --demo-seconds 6` on a board with its full SD pack mounted. It tests
all decoration names/orientations, lifetime/card retention, replacement memory,
voice ownership and playback resumption with both radios active. Watch the
physical demo for visual quality; protocol counters alone cannot establish it.
