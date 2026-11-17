# Experimental C6 port roadmap

## Approved milestone

**USB-first C6 Touch LCD 1.47 port:** owner approved in chat on 2026-11-17.
Implemented, installed and tested on the owner's exact board; see
[STATUS.md](STATUS.md) for evidence and limitations. Completion criteria:
animation playback/switching, touch input, bounded memory, original-firmware
backup and a verified USB installation. Owner confirmed the actual screen.

## Possible follow-up work — not yet approved

- Test integration with the installed Taby desktop app and document any custom
  hardware-target recognition requirements.
- Assess more animation clips or SD-card storage against memory and flash limits.
- Assess Wi-Fi/Bluetooth support only after measuring available resources.
- Assess simplified live mouth animation; the current S3 Rive renderer exceeds
  the C6 memory budget and cannot be enabled unchanged.

None of these proposals is a commitment or a prerequisite for the completed
USB-first milestone. Do not advertise other C6 boards or an official release
without their own hardware validation and maintainer approval.
