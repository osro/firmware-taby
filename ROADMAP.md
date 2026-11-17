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
- Desktop integration verified: installed 1.0.13 recognises USB; rotation now
  saves correctly; GUI Bluetooth scan/connect reports Paired and controls work.
  Native bridge identity, real choices and two reconnects passed. See
  [desktop evidence](STATUS.md#desktop-gui-result-on-dev2).
- The owner confirmed automatic Bluetooth reconnection after app restart;
  MCP grants remain unconfigured. Bluetooth client eye-mode
  controls and asset-version metadata are limited by the installed app.
- Owner confirmed the mouth artwork. Automatic rotation was repaired in dev.4
  after the failed dev.3 check; all four directions and upright text are now
  physically confirmed for text cards. Dev.5 also fixes the face-art alignment;
  owner and device confirm upright face in all four positions, with auto kept
  enabled. See [face result](STATUS.md#dev5-physical-face-result).
  Battery-only operation remains unverified.
- Pending external validation: MCP integration and official provisioning
  compatibility. Local pairing does not prove these.
- Card decoration is implemented, installed and device-tested with SD and both
  radios after the owner’s “advance!”. Host pixel/lifetime and device memory/
  ownership checks passed. Owner visual feedback is tentative; visual polish
  remains open. See [decoration evidence](STATUS.md#dev3-on-device-decoration-checks).
- Still not implemented: embedded UI clip behavior and Rive body/expression
  animation. Keep the simplified renderer’s limitation explicit; no PSRAM claim.

These items are not prerequisites for the completed USB-first milestone.
Do not advertise other C6 boards or an official release
without their own hardware validation and maintainer approval.

## Local fork preparation — complete

Owner requested a fork-ready local branch without pushing, then explicitly
required continued support for the original S3 boards. Preserve the verified
dev.5 device/build and validate the original board build paths during prep.
No remote fork, push, PR submission or release is authorized by this preparation.

The branch is prepared locally with current upstream source, separate C6
tooling/CI and successful builds/packages for all three targets. Original-board
source and CI matrix are preserved. No PR/draft, remote fork or push was made.
See [fork verification](STATUS.md#local-fork-branch-validation).

## Fork publication and README onboarding — complete

The owner subsequently supplied `osro/firmware-taby` and explicitly requested
the prepared branch be pushed there, superseding the preparation-only no-push
restriction. The branch was published; no PR/draft or release was created and
fork `main` was preserved. The owner's follow-up requested a new-board README
guide; separate C6 setup instructions now preserve the original S3 route.
See [publication and guide evidence](STATUS.md#fork-publication-and-c6-onboarding-guide--2026-11-17).

## Merge into fork main — complete

The owner explicitly requested merging the published feature branch into
`osro/firmware-taby` main, updating README to main, and adding C6 as the third
entry in the existing board table. The merged tree preserves the newer main
S3 mouth-animation fix and original-board code/build paths. C6's portable voice
header was synchronized with that shared code. All three builds/packages and
the combined host suite passed; physical hardware remains unchanged.
See [main integration evidence](STATUS.md#integration-into-fork-main--2026-11-17).
