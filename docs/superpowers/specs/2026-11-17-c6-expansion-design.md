# C6 feature expansion

The owner requested the remaining features after the verified USB port and
confirmed on 2026-11-17 that they can add a microSD card. Continue in small,
independently verified stages. Keep the known-working build and private backups.

## Architecture and constraints

- Target only Waveshare ESP32-C6-Touch-LCD-1.47/-M, ESP-IDF 5.5.2. Preserve S3
  runtime source and the current read-only onboard 16-animation fallback.
- Add optional FAT32 microSD playback of the complete 84-animation pack, using
  the existing SPI bus and GPIO4 chip select. Never format or erase a card.
  Read assets incrementally; never load the complete pack into RAM. Reject bad
  headers/lengths/checksums and fall back to flash on unavailable/bad media.
- Add saved brightness, eye-motion mode, orientation and preferred transport.
  Use a dedicated NVS namespace; storage errors must not erase unrelated NVS.
- Adapt cards, countdowns, progress and touch choices to the smaller display.
  Reuse the portable upstream UI parser; graphics must not require PSRAM.
- Add C6-specific orientation/IMU and calibrated battery voltage reporting.
  Distinguish measured voltage from unverified battery-percent estimates.
- Add Wi-Fi/local HTTP and BLE transports using the existing command semantics.
  UI/playback updates stay on one runtime thread; transport tasks enqueue work.
  USB must remain available for recovery. Do not log network credentials.
- Investigate a small raster mouth renderer for VOICE_MOTION instead of Rive.
  Advertise it only if implemented and exercised. No claim of identical S3 art.
- Official desktop-app support and cloud enrollment are external dependencies.
  Never fabricate a factory identity/secret or report official cloud success
  merely because local protocol commands work.

## Verification and handoff

Write focused portable tests before behavior changes; build on ESP32-C6, then
flash the backed-up board and test the added controls and previous animations.
Keep hardware records for each stage. The physical SD card, Wi-Fi credentials,
desktop-app login and battery/rotation feedback require the owner when needed;
continue independent work meanwhile. Do not call these checks complete without
the device/input. Record what remains in STATUS and ROADMAP.
