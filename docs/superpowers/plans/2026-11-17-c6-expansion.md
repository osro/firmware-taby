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
- [ ] Verify protocol/timing on device and obtain visual feedback.
- [ ] Review the whole expansion, update records, package verified artifacts.

## Progress

Use the matching SDD workspace ledger to record implementation and review
results. A blocked physical/card/service check is not a completed feature.

Implementation and current physical verification are recorded in [STATUS](../../../STATUS.md#feature-expansion--2026-11-17-in-progress). Unchecked external/card checks remain open until their actual results are recorded.
