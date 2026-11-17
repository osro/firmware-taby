# C6 feature expansion implementation plan

**Goal:** Extend the working C6 port toward the remaining local Taby features.
**Architecture:** Optional streaming SD storage, small portable feature modules,
and one owner of display/playback state; keep flash fallback and USB recovery.
**Tech stack:** ESP-IDF 5.5.2, C, existing portable Taby parsers, Python 3.12/Pillow.
**Spec:** [Expansion design](../specs/2026-11-17-c6-expansion-design.md).

## Global constraints

Exact board ESP32-C6-Touch-LCD-1.47/-M, 8 MB flash, no PSRAM. No card formatting,
whole-chip erase, eFuse changes, or factory-secret fabrication. Existing S3
runtime source and onboard fallback must remain usable. Report untested hardware
and external-service work explicitly. Network credentials stay private.

## Task 1: Saved preference model and storage

Create `main/c6_preferences.{c,h}` and `main/c6_preferences_nvs.c` under the C6
target, plus `tests/test_c6_preferences.py`. Portable model defaults: brightness
70, orientation 0, eye motion normal, preferred transport USB. Orientation
values 0,90,180,270,360(auto); eye values 0(normal),1(calm),2(still); transport
values 0(USB),1(Wi-Fi),2(BLE). Reject invalid values and trailing command junk.
Use upstream eye-motion parsing helpers rather than duplicate that grammar.

Interfaces: `c6_preferences_t`, `c6_preferences_defaults`,
`c6_preferences_valid`, and a portable command parser taking current state,
command and output state/reply. `c6_preferences_load/save` use ESP-IDF NVS and
return an error without erasing data. Namespace `taby_c6`, one versioned blob.
Do not edit app_main, board, CMake or the existing S3 files in this task.

- [x] Write behavioral tests for invalid stored settings, exact upstream replies,
  queries, valid mutation, mode validation and rejection without mutation.
- [x] Observe failures, implement portable behavior, run focused tests.
- [x] Implement NVS load/save with schema/version/length validation and defaults
  on missing/invalid data; no `nvs_flash_erase` recovery.
- [x] Review the task before integrating it into runtime.

## Task 2: Optional SD assets

Extend `tools/c6_assets.py` with an explicit full-library output mode while its
default output remains byte-for-byte compatible with the installed fallback.
Generate a separate full SD catalog with offsets, frame counts and max encoded
frame size. Use bounded SD file reads and shared-SPI initialization. No writes
to a mounted card from firmware. Add pack integrity/size checks and host tests
for stream boundaries, corrupt lengths and fallback behavior.

- [x] Generate a ready-to-copy `taby/` folder and ZIP for the owner.
- [x] Add storage abstraction and integrate with existing playback.
- [x] Build/test absent-card fallback; test real card when inserted by owner.

## Task 3: Display controls and reusable UI

Integrate preferences and add board rotation, QMI8658 polling and battery ADC.
Reuse upstream UI command parsing with a small renderer and flash-resident font.
Support headline/text/icon cards, progress, countdown, action and two-choice
cards; preserve choice-counter semantics. Add eye-motion frame plans compatible
with the converted clip frame timing. Separate display ownership from USB.

- [x] Unit-test pure preference/coordinate/timer/choice behavior.
- [x] Add runtime commands and advertise only implemented capabilities.
- [x] Build, flash, verify persistence across reset and previous playback.

## Task 4: Wireless and local app protocol

Add BLE GATT matching upstream UUIDs and local Wi-Fi setup/HTTP endpoints.
Transmit shared command/reply jobs through a bounded runtime queue rather than
allow transport tasks to mutate the display. Profile simultaneous memory use.
Keep USB available, reject unsupported commands honestly, redact credentials.

- [x] Host-test common dispatch/framing and bounds.
- [x] Build and test BLE advertising/connection and Wi-Fi/local control on hardware.
- [ ] Test desktop-app compatibility when installed and authenticated.

## Task 5: Lightweight voice and complete verification

Reuse the portable VOICE_MOTION parser and render small mouth states on a
pre-rendered neutral face. Preserve frame timing/stop semantics and cancel on
other presentation changes. Do not compile the PSRAM-dependent Rive renderer.

- [x] Add parser/timing/mouth-state tests and render samples for inspection.
- [x] Verify protocol/timing on device and obtain visual feedback.
- [x] Review the whole expansion, update records, package verified artifacts.

## Progress

Use the matching SDD workspace ledger to record implementation and review
results. A blocked physical/card/service check is not a completed feature.

Implementation and current physical verification are recorded in [STATUS](../../../STATUS.md#feature-expansion--2026-11-17-in-progress). Unchecked external/card checks remain open until their actual results are recorded.

## Desktop 1.0.13 continuation

The owner installed the app and confirmed the visual mouth demo. The resulting
bounded interface fixes are specified in [desktop compatibility](../specs/2026-11-17-c6-desktop-compatibility.md).

- [x] Reproduce/host-test canonical orientation names; preserve input aliases.
- [x] Implement/test the app's exact Bluetooth identity, choice and orientation framing.
- [x] Obtain scoped reviews and build the integrated dev.2 image.
- [x] Verify dev.2 raw/native-bridge identity, real choices and reconnects on hardware.
- [x] Verify actual GUI USB/Bluetooth controls and record results.
- [x] Package dev.2 separately from preserved dev.1 archives.
- [x] Observe automatic GUI reconnection after restarting the app (owner confirmed; automation notch-only).

## Task 6: Bounded card decoration

Owner's “advance!” on 2026-11-17 continues the approved remaining-features work.
Adapt the existing upstream title-decoration behavior to C6 without PSRAM;
this task adds decor only, leaving embedded clip behavior and voice body art
explicitly unsupported. Preserve shared S3 files and all current controls.

- Implement all 24 non-none decor names, normalized into the same eight
  families as `taby_reusable_ui.c`: shooting stars, comets, fireworks, heart
  burst, heart rain, gold rings, ribbon sweep and aurora sweep. Smaller-screen
  artwork may differ; names must not silently alias to an unrelated family.
- Restrict active decor to headline/text cards, matching upstream eligibility.
  Invalid enum/kind data must fail before replacing the current card. A valid
  duration of zero is an inactive effect for direct struct callers (the upstream
  command parser supplies its existing four-second default). Use uint64 math
  for duration/elapsed boundaries, including UINT32_MAX seconds and late ticks.
- Use at most eight persistent small LVGL objects for decor, with fixed backing
  arrays if shapes need points/pixels. No full-screen canvas or per-tick allocation,
  PSRAM, extra task, external asset fetch or source icon transformation required.
  All geometry fits both 172x320 and 320x172; keep readable text foreground.
- Model/pure helpers own normalization and lifetime/geometry as practical;
  renderer uses the existing single-owner tick. Duration expiry removes/hides
  decor while leaving the card. Replacement/hide/orientation changes must not
  leave old objects or restart the effect's epoch. Preserve text effects,
  countdown expiry, choice generations and rejected embedded clips.
- Own only C6 UI/model files, optional focused c6_ui_decor.c/.h, and UI tests/
  host harness. Parent owns CMake integration, version, docs, hardware and app.
- First write failing actual-C model and real-LVGL renderer tests; then implement.
  Cover every name/family, actual pixel changes across time and disappearance,
  invalid data preserving card, zero/large duration, both geometries, orientation
  epoch and hundreds of replacements/hides with stable LVGL heap. Include
  screenshot output through the existing C6_UI_SCREENSHOTS path for visual QA.
- Run focused tests while iterating and the full host suite once before committing
  only owned paths. Record red/green and limits in the task report. Do not access
  physical hardware, app data, radios or private backups; do not spawn agents.

Parent completion: inspect/review source, build/version/flash the backed-up
board; verify real effect cycles with SD and wireless, memory stability, prior
cards/playback and native Bluetooth regression. Obtain user visual feedback;
record exactly what was tested and preserve dev.2 packages.


Task 6 implementation, review and physical protocol/memory/SD/radio checks are
complete; see [dev.3 device evidence](../../../STATUS.md#dev3-on-device-decoration-checks).
Owner visual feedback remains tentative, with card/eye switching explained as
the now-finished stress test. Embedded clips and voice body art remain separate.

## Task 7: Repair automatic-rotation sensor setup

Owner asked to check rotation. On dev.3, auto mode accepted and IMU reported
available, but physical turns never changed the reported0°; owner confirmed
no rotation. Original left setting was restored. Root-cause source finding:
QMI8658 CTRL2 is written0x95, setting bit7 aST (self-test). QST/Waveshare
QMI8658C datasheet table24/page26 confirms bit7 enables self-test; bits6:4=001
select ±4g and ODR0101 is250Hz. Current8192LSB/g scaling matches ±4g.

- Own c6_board.c/.h plus focused actual-C sensor tests only. Parent owns version,
  app_main diagnostics, docs, hardware and packages. Shared S3 source untouched.
- Fix normal accelerometer configuration (CTRL2=0x15, self-test disabled),
  configure while disabled and enable accelerometer last. CTRL1 auto-increment,
  little-endian and oscillator remain configured as0x40; CTRL7 accel-only0x01.
  Read back and validate these settings before declaring imu_available. Cache
  actual ctrl1/ctrl2/ctrl7 readback in c6_board_status_t for parent diagnostics.
  On failed writes/readback/mismatch leave IMU unavailable without breaking LCD,
  touch, SD or unrelated NVS. Keep the existing ±4g8192 scaling.
- Keep the existing gravity mapping,1.2s settling/filter and geometry until real
  data identifies an axis-direction defect. No guessed flips, extra tasks,
  calibration persistence or auto setting changes.
- Write failing tests exercising actual initialization and polling logic with
  fake I2C register state and known gravity samples. A meaningful regression
  must distinguish a self-test-stuck sensor from changing gravity data, not
  merely compare a source string to0x15. Cover four orientation classifications,
  settling/flat/ambiguous retention, failed config/readback and read-error count
  without presentation changes. Extract focused production functions if needed;
  avoid a duplicate implementation of initialization/polling in tests.
- Run focused checks during development and full suite once before committing
  only owned paths; self-review. No hardware/app/private data access, subagents,
  new dependencies or broad board-driver rewrite. Report red/green and limits.

Parent adds a bounded read-only IMU command exposing cached samples/errors,
xyz_mg and config readback, preserving existing DIAG framing/size. Then review,
build/version/flash dev.4, owner cold restart and retest four physical positions.
Do not claim a physical repair before observed sensor changes and owner feedback.


Task7 is implemented, reviewed, installed and physically verified in dev.4.
Owner confirmed upright text in all four positions; live sensor vectors and
0/90/180/270 replies were recorded without errors. Original axis mapping and
prior fixed-left preference were preserved. See [results](../../../STATUS.md#dev4-physical-rotation-result).

## Task 8: Align packed face animation with upright card coordinates

Physical card auto-rotation now works on dev.4, but owner reports face portrait
in landscape and vice versa. Direct inspection of dist/c6-lcd-1.47/idle_01_loop.gif
shows the packed face sideways: its eyes are vertically stacked and mouth to
left. Packed RGB565 frames are172×280; rotate content90° counterclockwise to
recover natural upright280×172 face coordinates. Correct only animation drawing,
not gravity mapping or global panel orientation. UI text/card orientation is
already physically verified and must not change.

- Own only c6_board_draw_animation and focused real-C animation-drawing tests;
  optional pure C6 helper only if essential and parent owns CMake. Parent owns
  version, docs, app, hardware, packages. Other c6_board functions unchanged.
- Reuse all existing flash/SD assets byte-for-byte. Both decoded clips and live
  mouth frames pass through c6_board_draw_animation; rotate the complete input
  coherently so face/mouth alignment is retained without modifying voice art.
- Preserve aspect and center the upright280×172 image: in logical landscape
  320×172, render280×172 with20px side margins; in logical portrait172×320,
  render172×106 with107px top/bottom margins. Black-fill all margins so old
  content cannot remain. This defines expected adapted geometry, not S3 parity.
- Inverse sample mapping for upright coordinatesu∈[0,279],v∈[0,171] reads packed
  x=171−v,y=u. Nearest-neighbor fit into the two bounded destinations. Preserve
  RGB565 byte order; never mutate the input frame.
- Use the existing scanline buffer in small strips, with no new heap allocation,
  full-screen canvas, asset reconversion or card write. Every strip must fit
  board_draw_rect limits in all0/90/180/270 panel rotations. Keep DMA/fault
  ownership and immediate error propagation. Reject a NULL input safely.
- First write actual-C drawing tests that fail on the old path's wrong physical
  content orientation. Use an asymmetric source fixture with eye/mouth/corner
  markers; verify sample mapping, upright arrangement, exact fit/padding, input
  immutability and bounds in all four logical rotations. Include error/NULL/fault
  propagation; use ASan/UBSan where supported. Do not duplicate renderer logic
  as the test's implementation. If possible expose a temporary test harness
  way to export PPMs from an input raw frame for parent visual inspection.
- Run focused tests while iterating and the full host suite once before owned
  commit/self-review. No hardware/app/private-data access or subagents.

Parent completion: actual neutral-frame host preview, independent reviews,
dev.5 build/flash, owner cold restart and direct face rotation check in all
four positions, plus prior card orientation still correct. No success claim
based only on the previous text-card test.

Task8 physical face validation passed on dev.5 in all four positions, confirmed
by owner and sensor telemetry. Auto remained enabled; app reopened/BLE connected.
See [physical face result](../../../STATUS.md#dev5-physical-face-result).
