# Experimental C6 port roadmap

## Approved milestone

**USB-first C6 Touch LCD 1.47 port:** owner approved in chat on 2026-11-17.
Implemented, installed and tested on the owner's exact board; see
[STATUS.md](STATUS.md) for evidence and limitations. Completion criteria:
animation playback/switching, touch input, bounded memory, original-firmware
backup and a verified USB installation. Owner confirmed the actual screen.

## Feature expansion — in progress

The owner requested the remaining features in chat on 2026-11-17 and confirmed
they can add a microSD card. The expansion is split into independently checked
stages in the [design](docs/superpowers/specs/2026-11-17-c6-expansion-design.md)
and [plan](docs/superpowers/plans/2026-11-17-c6-expansion.md). Availability of the
physical card and external app/service setup does not imply completed tests.

- Implemented and device-tested: saved controls, basic cards/choices and
  simplified mouth motion. See [expansion evidence](STATUS.md#feature-expansion--2026-11-17-in-progress).
- Implemented and verified locally: all 84 SD animations, Wi-Fi/HTTP/mDNS and
  BLE, including full playback with both radios and bounded memory. Evidence is
  owned by [STATUS](STATUS.md#full-library-verification-on-the-final-build).
- Next approved integration check: the official desktop app/MCP recognizing the
  truthful custom target and sending working controls. No app is installed;
  the official download was blocked by Brave after following the vendor button,
  so manual download is the current owner handoff. No security bypass was attempted.
- Pending owner feedback: new mouth artwork and automatic-rotation axes; battery-only operation also remains unverified.
- Pending external validation: desktop-app/MCP custom-target recognition and
  official provisioning compatibility. Local commands do not prove these.
- Not implemented: advanced UI decor/clip behavior and Rive body/expression
  animation. Keep the simplified renderer's limitation explicit; no PSRAM claim.

These items are not prerequisites for the completed USB-first milestone.
Do not advertise other C6 boards or an official release
without their own hardware validation and maintainer approval.
