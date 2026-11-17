# C6 port status

This record describes the local experimental C6 port, based on upstream
`d781f2dc31d37841a3b5c6542f1e53f367f33b4b`. It is separate from the official S3
release. Scope was approved by the owner in chat on 2026-11-17 with “do it”.
See the [target guide](firmware/targets/c6-lcd-1.47/README.md) for behavior,
commands, build/install steps and recovery. See [ROADMAP.md](ROADMAP.md) for scope.

## Implemented and installed

- Independent ESP-IDF C6 application with the original Taby USB resolver,
  line reader and state/animation metadata; existing S3 source is unchanged.
- Waveshare ESP32-C6-Touch-LCD-1.47-M: JD9853 172×320 display and AXS5106L touch.
- Sixteen resized original clips, bounded RGB565 palette/RLE decoding, flash
  CRC verification, sequencing, idle return and safe DMA fault handling.
- USB animation control, diagnostics, brightness, tap and hold event polling.
- Firmware `0.1.0-c6-dev.1`, assets `0.1.0-dev`.

## Recorded verification — 2026-11-17

Platform: Apple Silicon macOS; Python 3.12.16, Pillow 12.0.0, ESP-IDF 5.5.2.
Board independently read as ESP32-C6FH8 v0.2 with 8 MB flash. The owner identified
the full Waveshare model and confirmed the image is visible, upright and moving,
and reported tapping and holding it.

- `tools/check.py` passed the upstream asset, board and layout checks.
- `python -m unittest discover -s tests -v`: **57 tests passed**, including six
  new C6 decoder/conversion checks. Failing codec tests were observed before
  implementation; fixtures cover byte order, palette bounds, truncation,
  output overrun, solid-color frames and clip duration preservation.
- All **656 generated frames** in all 16 clips were separately decoded with
  the actual compiled C decoder; clip bounds and total durations agreed.
- ESP-IDF C6 build succeeded. Initial installed app: 251,088 bytes; application
  partition: 2,031,616 bytes. Asset pack: 3,821,341 of 6,291,456 partition bytes.
- Full original 8,388,608-byte flash saved privately; checksum verified before
  installation. All four written regions passed esptool's write-hash check.
- On-device INFO matched the custom target, versions, geometry and pack hash.
- `tools/c6_verify.py`: every clip completed a full cycle. Sequencing,
  unsupported-animation handling, oversized-line recovery, PING and USB
  transport acknowledgement checks passed. The captured run rendered 853
  frames and recorded 17 completed cycles with zero decode/display errors.
- Free heap and minimum free heap stayed at **334,072 bytes** throughout the
  captured run; largest free block was 311,296 bytes.
- Touch telemetry recorded 3,383 successful reads, zero read errors, two short
  taps and ten total gesture events. The last recorded gesture was a tap;
  the owner separately reported testing a hold.
- A separate static review checked frame bounds, sequence handling, vendor
  touch/panel setup and DMA ownership. Its DMA fault/recovery and transport
  acknowledgement findings were fixed before installation.

The final build restricts the build graph to the application and its actual
dependencies, and places generated build/configuration files outside source
inputs. It compiled with no compiler warnings; the application shrank to
**223,664 bytes**. The updated bootloader/application were installed and their
write hashes verified; the partition table and asset pack were unchanged.
The complete on-device verification was repeated against this final build:
all 16 clips and protocol checks passed, again with 853 rendered frames and
17 completed cycles. Free/minimum heap stayed at **342,996 bytes**, largest
free block 319,488 bytes, with zero decode/display/touch-read errors and 3,409
successful touch reads. No additional physical taps were requested in this
second run; the first run retains the owner's touch and visual evidence.
See the [final-build record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-final-build.json).

The sanitized machine-readable hardware record and owner feedback are retained
in [the verification record](firmware/targets/c6-lcd-1.47/verification/2026-11-17.json).
The animation-pack SHA-256 is
`df6c970ec1dae9f6f78df9280b06df23527e1f73864602209b4cfda3087ad6b2`.

## Limits and distribution

This is a local development port, not an official upstream release. No remote
publication or upstream merge was performed. The original S3 builds were not
rebuilt during this task; their source stayed unchanged and host checks passed.
Physical desktop-app integration and MCP integration remain unverified; no Taby
desktop app was found in the usual local application folders. Wi-Fi, Bluetooth,
live lip-sync, account provisioning and reusable UI cards/timers are outside
this approved USB-first milestone. Other C6 display models are not validated.

Opening the serial port on this Mac can reset the C6 through USB control lines;
hardware verification used one persistent connection. The private original
flash backup is local and intentionally absent from version control/bundles.
