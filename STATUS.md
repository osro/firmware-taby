# C6 port status

This record describes the local experimental C6 port, based on upstream
`d781f2dc31d37841a3b5c6542f1e53f367f33b4b`. It is separate from the official S3
release. Scope was approved by the owner in chat on 2026-11-17 with “do it”.
See the [target guide](firmware/targets/c6-lcd-1.47/README.md) for behavior,
commands, build/install steps and recovery. See [ROADMAP.md](ROADMAP.md) for scope.

The installed development version is now `0.2.0-c6-dev.3`, with optional full SD
animations, saved controls, basic UI with card decoration, local wireless control and raster mouth
motion. Its current results and limits are in the expansion sections below;
the `0.1` milestone entries are retained as historical evidence.

## USB-first milestone — implemented and installed

- Independent ESP-IDF C6 application with the original Taby USB resolver,
  line reader and state/animation metadata; existing S3 source is unchanged.
- Waveshare ESP32-C6-Touch-LCD-1.47-M: JD9853 172×320 display and AXS5106L touch.
- Sixteen resized original clips, bounded RGB565 palette/RLE decoding, flash
  CRC verification, sequencing, idle return and safe DMA fault handling.
- USB animation control, diagnostics, brightness, tap and hold event polling.
- Firmware `0.1.0-c6-dev.1`, assets `0.1.0-dev`.

## USB-first recorded verification — 2026-11-17

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

### Follow-up feasibility assessment — 2026-11-17

After the owner asked about the remaining features, all 84 upstream rectangle
animations were converted in memory using `tools/c6_assets.py`'s existing
`encode_gif` routine from source revision `49300b4`, on the same Python/Pillow
environment. Their combined encoded size was **25,361,401 bytes**, excluding
inter-clip alignment, against the current **6,291,456-byte** asset partition.
The full library therefore needs a different storage/compression strategy;
the board's microSD slot is one option. This measurement uses the current
172×280, 64-color palette/RLE format and its existing sampling rule; it does
not prove that a more compact format could not fit.

Source inspection confirms additional work is required for UI cards, saved
settings, eye-motion modes, rotation, battery reporting and wireless transports.
The recorded free heap is headroom in the USB-only build, not a measurement
with Wi-Fi/BLE enabled. The current Rive renderer cannot be reused unchanged
without PSRAM. Upstream MQTT also requires factory identity credentials absent
from the public installation bundles; local firmware work alone does not
establish official cloud/account support. No additional firmware was written
during this feasibility assessment. Follow-up implementation is not complete.

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


## Feature expansion — 2026-11-17, in progress

The owner requested the remaining features and agreed to add microSD storage.
This supersedes the USB-first milestone's scope limits, without retroactively
changing its recorded evidence. Work is local on `codex/c6-lcd-147`;
preferences/UI/network/voice module commits are `3b0c67d`, `d31c0a4`, `8c1abfa`
and `1e81f74`. Parent integration was uncommitted during these stage checks.

Implemented and installed in firmware `0.2.0-c6-dev.1`: versioned NVS settings,
normal/calm/still eye motion, four rotations, QMI8658 automatic-orientation
selection, calibrated battery voltage, ten basic LVGL card kinds, 146 icons,
choice signals, queued transport ownership, local Wi-Fi/setup/HTTP and upstream
BLE service, plus a simplified live mouth renderer reusing the unchanged
upstream voice parser. No battery percentage is invented. Both radios remain
active after transport switches until reboot; USB remains available.

Recorded on the same C6 board/macOS/IDF platform:

- Core settings/control stage: saved brightness, eye mode and rotation survived
  an explicit hardware restart; 20 card checks covered all ten kinds in two
  orientations. Physical choice 1 was observed; advanced decor rejection
  preserved the existing card. Driver counters remained error-free.
- Core playback regression: all 16 onboard clips completed a cycle,858 frames
  and 17 cycles, with zero decode/display/touch errors. Free heap 300400 bytes,
  minimum 300352. These figures precede radio activation.
- Wireless stage: protected setup AP and both radios started. Mac Bluetooth
  discovered the exact board and completed two connections/reconnections,
  PING, cross-transport brightness, text-card display and state/power reads.
  Negotiated MTU 247. Final heap 67128, minimum 58004, largest 50176; no display,
  decode or touch errors. Wi-Fi join/HTTP were untested at that stage; later
  browser checks are recorded below.
- Voice stage: all 15 viseme IDs/jaw endpoints were exercised on hardware with
  both radios active; invalid timeline preservation, wrong/matching stops,
  natural completion, UI and animation cancellation passed. 107 voice frames
  rendered; zero decode/display/touch errors. This verifies protocol/counters,
  not owner visual approval of the new mouth artwork. Minimum heap 59876.

The optional full SD pack contains 84 clips / 3365 frames and 25361516 aligned
bytes, SHA256 `fefa27de0236133362194770f2d4f9b290726f34d93dd8abb5dc218afaded3bf`.
All frames were host-decoded using the real C decoder. The package is
`dist/taby-c6-lcd-1.47-sd-assets-dev.1.zip`. The owner's 63.2 GB card was detected in a Mac reader as FAT32. The package
was copied and every copied file hash verified, preserving existing contents;
no format was needed. See the [card-copy record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-card-copy.json).
After reinsertion, a processor-only reset left the card in an idle/timeout
state before filesystem access. Clearing USB control lines on this Mac
triggered that reset; asserting both before opening was observed to avoid it.
After the owner fully disconnected power, the card passed its full checksum
and the board reported all 84 animations. The cold-start check waited 27.9 s;
subsequent non-resetting opens preserved SD playback and counters. See the
[cold-start record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-sd-cold-start.json).
Firmware never formats or writes the card.

The dev.1 default build, with SD trace disabled, was **1887744 bytes** in the
2031616-byte application slot. It was installed with write hashes verified.
`python -m unittest discover -s tests -v` passed **123 tests**, no skips;
`tools/check.py`, Python compilation of verification tools and `git diff --check`
also passed. The complete SD playback run is recorded below.

On the preceding build, SD plus Wi-Fi/BLE passed all 15 live mouth states,
a complete 3600-frame voice timeline allocation, stop/cancellation and the
voice→one-second-card→idle regression. 112 speech frames rendered with zero
LCD/decode/touch errors. Minimum heap was 21256 bytes and final free heap 32712;
maximum voice timeline storage was bounded but retained for reuse by the
upstream parser at that stage. See the [combined voice record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-final-sd-voice.json).


Still incomplete: desktop-app/MCP recognition,
automatic-rotation axis feedback and battery-only operation. Advanced reusable
UI decor/clip behaviors and voice body/expression artwork are not implemented.
Factory/cloud provisioning is not established. The desktop site's automatic
app-download request returned HTTP403; manual app installation remains an
external step. No publication or upstream merge has occurred.


Expansion stage evidence: [controls](firmware/targets/c6-lcd-1.47/verification/2026-11-17-expanded-controls.json),
[core playback](firmware/targets/c6-lcd-1.47/verification/2026-11-17-expanded-core-playback.json),
[wireless](firmware/targets/c6-lcd-1.47/verification/2026-11-17-expanded-network.json),
[voice](firmware/targets/c6-lcd-1.47/verification/2026-11-17-expanded-voice.json),
and [host asset decoding](firmware/targets/c6-lcd-1.47/verification/2026-11-17-full-assets-host.json).

Final static review found and fixed four integration defects: timed cards after
voice now resume idle, landscape orientation replies round-trip into commands,
the bounded owner response holds the full animation catalog, and first-header
SD failures retry validated flash at startup and other presentation changes.
Actual-C regressions cover these boundaries. The final default build disables
the optional bounded SD startup trace. The completed tests and the successful full SD-library run are recorded here.


The owner used the phone setup form and the board acquired a home-network
address. Mac Brave then verified HTTP ping, health with connected/full-library
fields, the complete 84-entry queued catalog, malformed-command rejection and
mDNS-name HTTP access. Python/curl from the agent process could not reach the
local address; the full scripted HTTP suite was not run. See the [browser
verification record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-wifi-browser.json).
Wi-Fi was saved as the preferred boot transport. Home credentials remain in
private NVS and are excluded from records and archives.

During simultaneous voice-memory testing and phone provisioning, the earlier
build reached a 4492-byte minimum free heap. Its animation test stopped on a
strict memory-baseline assertion after 21 clips, with zero display/decode/touch
errors; this was not a completed full-library run. The latest build therefore
uses one bounded request allocation sized to each command/caller response,
releases the upstream voice vectors on presentation reset, and caps concurrent
HTTP sockets at 3. Actual-C/C++ allocation regressions and the exact-fit response
boundary passed. Final physical measurements followed the owner-confirmed full power cycle,
as recorded below; the fresh full-library run also completed successfully.


### Final memory-build device checks

On the latest installed binary, the SD library loaded and saved home Wi-Fi
reconnected automatically after complete power removal. Both BLE connection
rounds, cross-transport controls and state/power reads passed with SD mounted;
see the [final network record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-final-memory-network.json).
The real home profile was preserved; the earlier generated-profile persistence
check was not repeated on the owner's credentials.

All 15 mouth states, maximum 3600-frame timeline allocation/release, stops,
presentation cancellation and timer expiry passed again. After timeline stop,
free heap recovered by more than 8 KB as required. Final free heap 49620 bytes,
minimum 37680, largest 34816; no decode/display/touch errors. See the
[final voice/memory record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-final-memory-voice.json).
The final network run ended with free heap 49496, minimum 37680 and largest 32768.
These are actual tested conditions, not an unlimited-concurrency guarantee.

The simplified mouth artwork was inspected in a [rendered viseme contact sheet](firmware/targets/c6-lcd-1.47/verification/2026-11-17-voice-visemes.png). It shows the actual raster shapes, rather than claiming Rive pose parity.


### Full-library verification on the final build

`tools/c6_verify.py --radios` completed a full cycle of **all 84 SD
clips**. Playback advanced 4255 frames and 153 cycle counters (including looping/idle
transitions). Decode, display and touch-read error counters stayed at zero.
Free heap was **49516 bytes at both start and finish**, global minimum
37680, largest free block 32768. Both radios stayed active, saved Wi-Fi remained
connected, and the preferred boot transport was restored to Wi-Fi. The device
was returned to idle after the check. See the [complete playback record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-final-full-sd-playback.json) and
[final installed-build record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-final-expanded.json).

Application SHA256:
`d0b7a0011d8b12ea00dbaae1389f1a0634a5aa1f5a1df6e4bf0ab9afe87fb35c`.
The existing flash fallback, partition table and original private backup were
preserved. Shared S3 runtime/component source has no diff from the USB-first
base. This remains a local experimental build, with the external/parity limits
listed above; successful local control does not establish official cloud or
desktop-app compatibility.


### Local distribution and remaining handoff

Implementation and verification are committed locally at
`36fe1379b041f5763c0b2368ef316490d0ce1d5d`. The following ignored local archives
were generated from that revision, integrity-checked and SHA256-checked:

- `dist/taby-c6-lcd-1.47-expanded-dev.1.zip`: four checked flash images,
  installation/recovery guide, sanitized verification and 31 dependency notices.
- `dist/taby-c6-lcd-1.47-expanded-source-dev.1.zip`: Git archive whose commit
  comment matches the source revision above.
- `dist/taby-c6-lcd-1.47-sd-assets-dev.1.zip`: separately prepared full SD pack,
  already copied and hash-verified on the owner's card.

Archive sizes/hashes and exclusions are in the [distribution record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-distribution.json).
These are local packages, not an upstream release. The private original-board
backup and credentials are excluded. This later documentation update records
package creation after the archived source revision; it changes no runtime code.

The normal official desktop-download page was also tried in Brave. Its download
button reached the vendor's `updates.heytaby.com` DMG, but Brave blocked it with
`ERR_BLOCKED_BY_CLIENT`. The agent stopped at that browser security block and
asked the owner to handle the official download. No desktop app was installed,
no model was downloaded, and desktop/MCP pairing remains pending. The earlier
CLI HTTP403 is not evidence that the official download button itself is broken.

A separate one-minute silent mouth demo completed and returned to idle without
reported driver/decode/touch errors; see its [record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-live-mouth-demo.json).
Owner visual feedback was requested at this stage and later confirmed below. The renderer's
mouth-only limitation remains; advanced card effects, live body/eye poses,
official cloud provisioning, automatic-axis feedback and battery-only operation
remain outside the verified result.


### Desktop compatibility continuation — 2026-11-17

The owner confirmed that the live mouth looks correctly positioned and moves
clearly, and manually installed the official desktop app. `/Applications/Taby.app`
reports version 1.0.13. Its actual Physical Taby settings page reports connected
by USB, firmware 0.2.0-c6-dev.1 and SD assets 0.2.0-sd. This supersedes the earlier
download/install handoff; full app control checks are now underway.

Static inspection of the installed client's orientation contract found it
accepts only auto/left/right/90/270. Firmware reply names rotate_90/rotate_270
therefore need correction even though those input aliases remain useful.
Commit 9afa16a applies one shared canonical-name helper to both reply paths;
124 host tests passed without skips and scoped review found no issue. This
change was committed before the dev.2 installation recorded below.

The client also requires a Bluetooth protocol 2 identity and choice state.
The current legacy link passed direct BLE tests but is correctly shown by the
app as unsupported for its newer pairing flow. The additional raw JSON identity
(UUID suffix 0007) and choice (0006) interfaces were required; their implementation and native tests are recorded below.
They do not alone establish GUI pairing. No app bundle or personal app data
has been modified. USB reset handling was considered from source inspection,
but actual USB recognition/full-SD metadata contradicts an assumed always-reset
failure; no reset-disable register or recovery behavior has been changed.


The dev.2 application is now installed (1890064 bytes, write hash verified)
after scoped clean reviews and 130 host tests without skips. It includes the
canonical orientation helper, shared USB/BLE version constant, and the exact
Bluetooth v2 identity, choice and orientation interfaces. SD 84 and saved Wi-Fi
loaded after the owner-confirmed cold start. The input aliases remain accepted;
no original animation, SD pack or S3 source changes were needed.

The actual installed 1.0.13 native Bluetooth bridge completed two scan/connect/
disconnect rounds using the exact board selected through USB identity and the
advertised suffix. Identity/state matched USB; text, timer, confirmation, stop
and numeric orientation set/readback passed. The owner tapped LEFT then RIGHT;
the bridge read counters 1 and 2 and both agreed with the legacy USB choice reply.
There were zero LCD/decode/touch errors. Final free heap 49044, minimum 39036,
largest 32768. See the [native bridge record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-desktop-native-bluetooth-v2.json).
The initial diagnostic harness supplied Bleak's differently cased peripheral
identifier and was rejected before GATT discovery; using the helper's exact
opaque scan identifier fixed the harness. It was not a firmware failure.

The [initial USB/app record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-desktop-usb-dev1.json)
retains the owner's actual rotation error and confirms SD playback survived app
use. The app was closed normally during installation and native-bridge checks, then
reopened for the GUI tests recorded below. MCP grants and cloud enrollment
were not configured.

Dev.2 also passed the complete voice/memory regression: all 15 mouth states,
maximum 3600-frame timeline allocation/release, stop/cancel and timed-card expiry.
Free heap returned to 49064 bytes, minimum 35460, largest block 32768; zero
decode/display/touch errors. See the [dev.2 voice record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-desktop-v2-voice.json).
The complete 84-clip cycle evidence remains the dev.1 run above; assets and
decoder are unchanged, and that long playback suite was not repeated for dev.2.

### Desktop GUI result on dev.2

The actual installed app's Physical Taby page recognised dev.2 over USB with
SD assets 0.2.0-sd. Test the display reported “Animation sent to your Taby.”
Rotate 90° selected successfully and reported “Display orientation saved on
Taby.”, fixing the owner's reproduced error; Left side was restored.

Enable on USB-connected Taby and Search found the board. Connect changed the
page to **“Connected over Bluetooth” / “Paired”**. The display test was clicked
again on that connection, and numeric rotation saved successfully over
Bluetooth; Left side was restored again. See the [GUI record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-desktop-gui-v2.json).
The client's enable flow saves Bluetooth as the preferred boot transport;
existing Wi-Fi profiles remain stored. USB remains available as a backup.

The Bluetooth page disables eye movement with “Needs a Taby software update”
and shows Assets Unknown. Static client transport inspection shows no eye-mode
API or asset-version field on that path. These client limits do not remove the
firmware's USB controls or SD pack. A subsequent USB check still reported all
84 SD clips and left orientation, 799 rendered frames, zero driver/decode/touch
errors, free heap 49132 and minimum 35460. The app was then quit and reopened
normally; automation sees only its notch, so automatic app-start reconnection
has not been observed. The successful initial GUI pairing and two native
reconnections are distinct recorded results. No battery-only test was run.

The dev.2 code was rebuilt and installed with write-hash verification; final
host verification passed 130 tests, no skips, plus asset/layout checks, tool
compilation and whitespace checks. Application SHA256:
`aa280a09a12e56898fd852d77779838dcb0998f02986a359945317ea17b6abd3`.
This is a local experimental port; no upstream release or official cloud enrollment.

### Dev.2 local distribution

Integrated source and device evidence are committed at `8a166c22387c1cb395f25b540512aa87df385688`.
New binary and source archives are `dist/taby-c6-lcd-1.47-expanded-dev.2.zip`
and `dist/taby-c6-lcd-1.47-expanded-source-dev.2.zip`. Both passed ZIP integrity,
source commit and image-hash checks. The full SD archive is unchanged and need
not be copied again. All previous dev.1 packages and the private original-board
backup are preserved. No remote publication occurred. Exact sizes/hashes are
in the [dev.2 distribution record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-distribution-dev2.json).
This subsequent documentation-only commit records package creation; runtime
code matches the archived source revision.

### Reconnection follow-up

After the preceding app restart, the owner confirmed that Physical Taby says
“Connected over Bluetooth.” This closes the app-start reconnection check by
owner observation; automation still sees only the notch. See the [owner record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-desktop-reconnection-owner.json).
The existing dev.2 archives retain their earlier, accurate pending status.

### Card decoration continuation — in progress

The owner asked to “advance!” on 2026-11-17. Bounded title-card decoration is
being added through the existing parser, UI model and single renderer owner.
The decoration version is `0.2.0-c6-dev.3`.
Embedded clip behavior, voice body artwork and external cloud/MCP gaps remain
separate. The dev.3 application was installed after the checks recorded below.

Task 6 is implemented at `4c1980a`: all 24 title/text decoration names map to
the original eight families, adapted as bounded line artwork. There are eight
small objects, approximately 1252 static backing bytes, no full-screen canvas
and no new rendering task. Expiry hides decoration while retaining the card;
rotation keeps the original epoch. Embedded clips remain unsupported.

Host verification passed **135 tests**, no skips, including all names in both
geometries, actual pixel motion/expiry, text foreground, aliases, invalid-card
preservation, large durations and rotation. Added retained LVGL heap was 2240
bytes; 384 host lifecycle cycles recovered the pool exactly. The host allocator
differs from device malloc. Two cleanup-failure tests establish that the device
verifier attempts all restoration steps. Task/final reviews approved after
that verifier correction. See the [host record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-c6-decor-host.json)
and [host-rendered preview loop](firmware/targets/c6-lcd-1.47/verification/2026-11-17-c6-decor-host-preview.gif).

ESP-IDF 5.5.2 C6 build passed without compiler warnings: **1892672 bytes**,
7% slot headroom. The application-only write passed its hash check; NVS,
bootloader, partitions, flash fallback and the card were untouched. See the
[installation record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-c6-decor-install.json).
Application SHA256: `a036c5648c1627cc94f3c960900cc48939e445f052002a7eed949db866409556`.
The owner was asked for a cold power restart to initialize this card cleanly.
Physical effect cycles, SD/radio memory, Bluetooth regression and new artwork
feedback remain pending; no hardware effect success or dev.3 distribution is
claimed yet. Taby's desktop app is closed during the update/test stage.

### Dev.3 on-device decoration checks

After the owner-confirmed cold restart, all 84 SD animations were mounted and
Wi-Fi reconnected. With both radios active, all 24 decoration names were
accepted in both orientations, all eight family expiry checks retained their
card, 120 decorated-card/clear cycles preserved heap, and decorated-card↔voice
ownership plus timed-card expiry passed. Idle SD frame progress resumed. The
final report had free heap **47960 bytes**, minimum **41336**, largest block
30720 and zero decode/display/touch errors. See the [device record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-c6-decor-device.json).

The first run completed effect/churn/voice/demo stages but failed its final
1.5-second idle-progress assumption. The saved Calm mode intentionally holds
the first frame for 3–7 seconds. A focused check confirmed normal explicit
animation progress; the verifier now allows the full nine-second Still hold
without changing the owner's preference. The complete repeated check passed;
no firmware change or second flash was needed. See the [timing investigation](firmware/targets/c6-lcd-1.47/verification/2026-11-17-c6-decor-idle-wait.json).

The owner saw the eight-family six-second demo and gave tentative feedback
(looked okay but uncertain), rather than full visual approval. Their report
of switching between Taby's eyes and CELEBRATE corresponded to the deliberate
memory stress test's card/clear alternation, which was explained. That test
has finished; ordinary decoration stays with its card. Visual polish remains
open. The complete 84-clip cycle remains inherited dev.1 coverage; dev.3
checks SD retention and resumed playback, without claiming another full cycle
of every clip. The final native Bluetooth regression is recorded below.

The installed desktop 1.0.13 native Bluetooth bridge passed two further
scan/connect/disconnect rounds on dev.3: identity matches USB, choice schema,
animation/text/timer/stop commands and numeric orientation set/readback all
passed. No new physical touch input was requested; the earlier LEFT/RIGHT
owner test remains recorded dev.2 evidence. Final heap 47856, global minimum
38360, largest block 30720; no decode/display/touch errors. See the [dev.3
Bluetooth record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-c6-decor-bluetooth.json).
Saved left orientation and active Bluetooth mode were restored. The Taby app
was reopened normally after all device tests; only its notch was directly
observed at that reopening. No new GUI connection-status claim is made.
All repeated card/clear stress testing has stopped. The installed firmware
runtime remains byte-for-byte the hash above; only its host verifier's wait
was corrected after physical testing. The new package is recorded below.

### Dev.3 local distribution

Source and device evidence are committed at `5a6f2230c4392075e596ea465790974202c145d6`.
The new binary/source archives are `dist/taby-c6-lcd-1.47-expanded-dev.3.zip`
and `dist/taby-c6-lcd-1.47-expanded-source-dev.3.zip`. ZIP integrity, all four
image hashes, source archive commit comment and 31 dependency notices were
checked. Previous dev.2 and SD archive hashes are unchanged; the original
private backup remains excluded. Exact sizes/hashes are in the [distribution
record](firmware/targets/c6-lcd-1.47/verification/2026-11-17-distribution-dev3.json).
The full card pack does not need copying again. This is a local experimental
package; tentative visual feedback and the remaining clip/body/cloud/MCP
limitations remain explicit. No remote publication or upstream merge occurred.
This later documentation-only commit records creation of those archives.
