# C6 compatibility with desktop Taby 1.0.13

This refines the approved desktop-validation stage of the C6 expansion. The
owner installed the official app manually and confirmed the mouth demo looks
correct. The app's actual Physical Taby settings recognised USB and the full
SD asset version. The owner then reproduced a rotation error: the LCD rotates,
but the app says the operation is unsupported.

## Verified interface requirements

- Keep the truthful `c6-lcd-1.47` target and the stable development chip-derived
  device ID. No factory/cloud credentials or identity are invented.
- The installed client accepts orientation modes `auto`, `left`, `right`, `90`
  and `270`. Firmware must emit those names in both the saved-mode and actual
  effective-orientation fields; `rotate_90`/`rotate_270` remain accepted inputs.
- The existing Bluetooth service ends in UUID `0001`, command write in `0002`,
  event notify/read in `0003`, state read in `0004`, and power read in `0005`.
- Add raw readable choice JSON on `0006`:
  `{"signal":N,"choice":"left|right|none"}`. Preserve the actual board counter;
  map existing `choice_1`/`choice_2` selections without changing USB replies.
- Add raw readable identity JSON on `0007`, with exactly three keys:
  `{"protocol":2,"deviceId":"...","firmwareVersion":"..."}`.
- Desktop Bluetooth orientation events use
  `OK DISPLAY_ORIENTATION <degrees> <mode> <orientation> <auto 0|1> rectangle`.
  USB/HTTP continue to return their JSON envelope.
- Once these interfaces exist, USB capability `bluetooth_v2` lets the installed
  app offer its Bluetooth pairing controls. Version `0.2.0-c6-dev.2` separates
  this compatibility build from the already archived/tested dev.1 build.

These requirements were checked against the installed app's static contracts,
its USB/Bluetooth transport modules, and its native Bluetooth bridge. No app
bundle or personal app data is modified. A source-only USB-reset hypothesis
was not adopted: live USB recognition and subsequent SD telemetry show the app
can retain the full library. No reset-disable register or recovery path changes.

## Runtime constraints

The existing single presentation owner remains responsible for choice state
and orientation. BLE callbacks copy bounded caches or queue commands; they do
not wait for the owner. Populate choice with a real owner read before initial
advertising and retain the last real global snapshot across reconnect. Reject
stale per-session command replies using the existing generation/host queue.
An owner request already accepted is not cancelled merely by disconnecting.

Identity and choice caches remain small. Reuse the existing BLE worker and
cJSON dependency, validate all structured response fields, and free temporary
JSON trees on every path. No extra worker, external service, model download,
account setup, factory secret, or eFuse operation is required.

## Completion criteria

1. Compiled-C orientation and wire-format regressions, source review and the
   full host suite pass. Build and hash-verify the new application installation.
2. On the exact board, verify the raw identity/choice characteristics, genuine
   identity consistency, canonical orientation read/set, direct legacy support
   and reconnects. Keep SD and saved network data intact.
3. Exercise the actual installed native bridge, including text, timer, stop,
   real left/right choices and reconnect identity. This is distinguished from
   the app's GUI remembering a Bluetooth selection.
4. Verify the installed app's USB controls and Bluetooth scan/connect/display
   status. Restore the original left orientation after the requested test.
5. Record actual outcomes and any client limitations in STATUS/ROADMAP/guide;
   create a separate dev.2 package. Preserve the dev.1 images and original
   private backup. MCP grants and cloud enrollment are separate from pairing.
