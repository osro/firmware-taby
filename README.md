<a id="start-here"></a>

> **Experimental C6 addition:** this fork retains the original S3 boards and adds
> **Waveshare ESP32-C6-Touch-LCD-1.47-M**. Start with the
> [C6 setup guide below](#c6-setup). The C6 target is separate from the official
> S3 releases. See the
> [fork/build guide](FORK.md) and [verified results and limits](STATUS.md).

<table>
<tr>
<td width="620" valign="middle">
<h1>Physical Taby</h1>
<p><strong>Your Taby, with a little place on your desk.</strong></p>
<p>
  <a href="https://www.heytaby.com">Meet Taby</a> &nbsp;·&nbsp;
  <a href="https://www.heytaby.com/setup">Get the desktop app</a><br>
  <a href="#bring-taby-to-life">Install firmware</a> &nbsp;·&nbsp;
  <a href="https://github.com/TRIIIS-LABS/firmware-taby/releases/download/prints-1.64-1.0.0/taby-1.64-print-pack-1.0.0.zip">Download 3D print files</a>
</p>
</td>
<td width="240" align="center" valign="middle">
<a href="https://www.heytaby.com"><img src=".github/confirmation.gif" width="200" alt="Taby celebrating with its confirmation animation"></a>
</td>
</tr>
</table>

**Taby is your personal AI assistant and computer buddy.** Chat, manage tasks,
set reminders, and stay focused with the [Taby app](https://www.heytaby.com/setup).

The app works on its own. Want Taby on your desk too? This repo has the
**firmware, animations, and case files** to build a physical Taby.

**Let's make one:** [1. Choose your board](#supported-displays) →
[2. Install the firmware](#bring-taby-to-life) →
[3. Connect to Taby](#connect-to-taby) →
[4. Print a case](#a-home-for-your-taby)

---

<a id="supported-displays"></a>

## 1. 🖥️ Choose your board

You'll need a supported board and a **USB data cable**.

<table>
<tr>
<td width="290" valign="top">
<h3>Taby 1.64</h3>
<p>The rectangular one.<br>280 × 456 AMOLED · 84 animations</p>
<p><strong>Waveshare ESP32-S3-Touch-AMOLED-1.64</strong></p>
<p><a href="https://www.waveshare.com/esp32-s3-touch-amoled-1.64.htm?&amp;aff_id=134369">Get the board ↗</a> &nbsp;·&nbsp; <a href="#a-home-for-your-taby">Print its case</a></p>
</td>
<td width="290" valign="top">
<h3>Taby Round 1.32</h3>
<p>The round little one.<br>466 × 466 AMOLED · 27 animations</p>
<p><strong>Waveshare ESP32-S3-Touch-AMOLED-1.32</strong></p>
<p><a href="https://www.waveshare.com/esp32-s3-touch-amoled-1.32.htm?&amp;aff_id=134369">Get the board ↗</a> &nbsp;·&nbsp; Case coming soon</p>
</td>
<td width="290" valign="top">
<h3>Taby C6 1.47</h3>
<p>The small LCD one · Experimental.<br>172 × 320 LCD · 16 onboard animations<br>84 animations with FAT32 microSD</p>
<p><strong>Waveshare ESP32-C6-Touch-LCD-1.47 / -M</strong></p>
<p><a href="https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47">Board details ↗</a> &nbsp;·&nbsp; <a href="#c6-setup">Setup guide</a><br>Case not included</p>
</td>
</tr>
</table>

**C6:** physically tested on the **-M** variant. Supports touch, automatic rotation,
saved settings, USB, Bluetooth, local Wi-Fi, basic cards and simplified talking
mouths. The face keeps its aspect ratio, with black margins on this narrow screen.

Use the [C6 setup guide](#c6-setup). This board needs its own firmware; the S3
AMOLED images and installer do not support it. Other ESP32-C6 boards have not
been validated. See [tested results and remaining limits](STATUS.md).

For the original S3 boards, check [board compatibility](INSTALL.md) before buying.

**Affiliate disclosure:** The two S3 product links above are affiliate links.
Purchases through them may earn us a commission and help support Taby development.

---

<a id="install-with-your-ai-agent"></a><a id="bring-taby-to-life"></a>

## 2. ✨ Install the firmware

Choose your route: **[C6 LCD 1.47/-M](#c6-setup)** or
**[original S3 AMOLED boards](#s3-setup)**.

<a id="c6-setup"></a>

### ESP32-C6 Touch LCD 1.47 / -M

This fork currently provides a **source build**, not an official prebuilt C6
release. Source version `0.2.0-c6-dev.6` has passed build, package and host checks;
the physical-board results in [STATUS.md](STATUS.md) were recorded on dev.5.

You need the exact board above with **8 MB flash**, a USB **data** cable,
Git, Python **3.12**, and **ESP-IDF 5.5.2**. For all 84 animations, also get a
FAT32 microSD card and a computer-connected card reader. The board's USB port
does not expose the card as a drive. The commands below use macOS/Linux shell
paths; on Windows, use the ESP-IDF shell and `.venv\Scripts\python.exe` in place
of `.venv/bin/python`.

**1. Get this fork and generate the assets.** From a normal terminal:

```sh
git clone --branch main https://github.com/osro/firmware-taby.git
cd firmware-taby
python3.12 tools/bootstrap.py
.venv/bin/python -m pip install -r tools/requirements-c6.txt
.venv/bin/python tools/c6_assets.py
.venv/bin/python tools/c6_assets.py --sd-card
```

**2. Build and package.** Install and activate ESP-IDF **5.5.2** using
[Espressif's ESP32-C6 setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32c6/get-started/index.html).
In that SDK's shell, return to the repository root. Keep its Python environment
separate from the tools `.venv`; do not activate the tools environment for the build.

```sh
python tools/c6_build.py
.venv/bin/python tools/c6_package.py --output dist/c6-development
```

The package command requires a new output directory. It creates `firmware.zip`,
`sd.zip`, checksums and an installation guide under `dist/c6-development/`.
Use a new directory name if you have already packaged a build.

**3. Back up and install.** Connect the board and close Taby or other serial
apps. List ports without resetting the board:

```sh
.venv/bin/python tools/device.py ports
```

Replace `PORT` below with its port, such as `/dev/cu.usbmodem…`, `/dev/ttyACM0`
or `COM5`:

```sh
.venv/bin/python -m esptool --chip esp32c6 --port PORT flash_id
```

Continue only if the board marking matches and the chip reports **8 MB** flash.
Make a private backup in `.local/`, which is ignored by Git:

```sh
mkdir -p .local/backups
.venv/bin/python -m esptool --chip esp32c6 --port PORT \
  read_flash 0 0x800000 .local/backups/original-esp32-c6-8mb.bin
```

Check that the backup is **8,388,608 bytes** and record its SHA-256 before
installing. Installation replaces the application and partition layout:

```sh
.venv/bin/python -m esptool --chip esp32c6 --port PORT --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 dist/c6-development/bootloader.bin \
  0x8000 dist/c6-development/partition-table.bin \
  0x10000 dist/c6-development/firmware.bin \
  0x200000 dist/c6-development/assets.bin
```

Install all four matching files. Do not use the S3 installer, `--force`, a full
chip erase or eFuse changes. See the [detailed installation and recovery guide](firmware/targets/c6-lcd-1.47/README.md#install-and-verify)
if the board is not detected or you need to restore your backup.

**4. Add the full animation library (optional).** Extract
`dist/c6-development/sd.zip` on your computer and copy its **`taby/` directory
to the root of the FAT32 card**, so it contains `/taby/assets.bin` and
`/taby/assets.json`. Use this build's matching **`0.2.1-sd`** pack; the earlier
dev.5 pack is different. Safely eject the card, unplug the board until its
screen is dark, insert the card, then reconnect power. Allow roughly **30
seconds** for the full checksum check. Without a valid card, Taby uses its 16
onboard clips. The firmware never formats or writes the card.

**5. Check the screen and connect the app.** With Taby closed, you can run:

```sh
.venv/bin/python tools/c6_verify.py --port PORT
```

This plays every available animation and checks device counters. Watch the
actual face too: eyes should stay side by side with the mouth below. Once the
check exits, open the [Taby desktop app](https://www.heytaby.com/setup), then
**Settings → Physical Taby** and connect over USB. For Bluetooth, use the app's
pairing controls; USB remains available for setup.

For manual controls or Wi-Fi setup, close the app and open one USB console:

```sh
.venv/bin/python -m serial.tools.miniterm PORT 115200 --dtr 1 --rts 1 --eol LF
```

Send one command per line: `DISPLAY_ORIENTATION auto` enables automatic
rotation; `EYE_MOTION calm` selects calmer eyes. For Bluetooth, send
`TRANSPORT_MODE ble` and `TRANSPORT_DEFAULT ble` to start it and save the boot
preference. Exit the console with **Ctrl+]** before reconnecting the app.

For Wi-Fi, send `SETUP_START`, then `SETUP_INFO`. Join the displayed temporary
network from a phone and open **http://192.168.4.1/setup**. Choose “Stay
connected” or “Use without internet” if prompted, then enter your home **2.4
GHz** Wi-Fi details in the form. Keep the setup response private: it contains
the temporary password. `TRANSPORT_DEFAULT wifi` saves Wi-Fi as the boot choice.
The [target guide](firmware/targets/c6-lcd-1.47/README.md#behavior) lists all controls.

If SD startup times out after flashing or a serial reset, fully unplug the
board until the screen is dark, wait ten seconds, then reconnect with the card
inserted. A processor reset can leave the card powered in the wrong state.

**Known limits:** Bluetooth eye-mode controls and asset-version display depend
on desktop-app support (tested app: 1.0.13); USB can control eye modes. Live
speech uses simplified mouth art, with no Rive body/expression animation.
Embedded card clips are unsupported, battery-only operation is unverified,
and official cloud enrollment and MCP setup are not established. Original S3
boards retain their upstream features.

<a id="s3-setup"></a>

### Original ESP32-S3 AMOLED boards

Plug your board into your computer with the USB data cable. Open an AI agent
that can run commands on that computer, then copy and send this prompt:

```text
Help me install Physical Taby on my connected device.
Read and follow this guide:
https://github.com/TRIIIS-LABS/firmware-taby/blob/main/INSTALL.md

Install any missing USB/ESP32 tools and list the connected ports. If Taby is
already running, read its board metadata. For a blank board or vendor demo,
identify the exact board/revision from its marking or ask me when uncertain;
do not require device identify to succeed before the first flash. Install
the complete matching release, verify it, and play an animation so I can
check the screen.
Then help me connect it to the Taby desktop app.
```

Prefer doing it yourself? **[Follow the installation guide →](INSTALL.md)**

Official S3 release linked by this guide: **[1.3.0](https://github.com/TRIIIS-LABS/firmware-taby/releases/tag/v1.3.0)**.
It is separate from this fork's C6 development build.

---

<a id="connect-to-taby"></a>

## 3. 💛 Connect to the Taby app

Once the animation plays correctly, **[get the Taby app](https://www.heytaby.com/setup)**.
For the original S3 boards, follow the [app connection guide](INSTALL.md#5-use-it-with-taby).
For the experimental C6 board, use the [C6 connection steps and limits above](#c6-setup).

We maintain the app connection. For your own integration, use the
[device commands](firmware/README.md#talk-to-taby).
See the guide for [current MCP support](INSTALL.md#5-use-it-with-taby).

---

<a id="print-a-case"></a><a id="3d-print-your-case"></a><a id="a-home-for-your-taby"></a>

## 4. 🏡 Print a case

Your board needs a case. Choose one for your screen, or design your own.
This fork does not include a C6 LCD 1.47 case; the S3 cases below are for their
named boards.

<table>
<tr>
<td width="430" valign="top">
<h3>Taby 1.64 cases</h3>
<h4>Desktop case · Available now</h4>
<p align="center"><img src="hardware/amoled-1.64/V1/desktop-case/preview.png" width="170" alt="The printable base, back, and handle for Taby 1.64"></p>
<p>Base, back, and handle.<br>STL · 3MF · G-code</p>
<p><strong><a href="https://github.com/TRIIIS-LABS/firmware-taby/releases/download/prints-1.64-1.0.0/taby-1.64-print-pack-1.0.0.zip">↓ Download the print pack</a></strong></p>
<p><a href="hardware/README.md">Printing guide</a> &nbsp;·&nbsp; <a href="hardware/amoled-1.64/V1/desktop-case">Individual files</a></p>
<hr>
<p>– Star variation (coming soon)<br>
– Flower variation (coming soon)<br>
– Spaceship variation (coming soon)</p>
</td>
<td width="430" valign="top">
<h3>Taby Round 1.32 cases</h3>
<h4>Round case · Coming soon</h4>
<p>Print files aren't available yet.</p>
<hr>
<p>– Star variation (coming soon)<br>
– Flower variation (coming soon)<br>
– Spaceship variation (coming soon)</p>
</td>
</tr>
</table>

For the Taby 1.64 desktop case, also get an **extra-strong neodymium block
magnet, 20 × 10 × 2 mm**, and **self-adhesive rubber grip tape** or a similar
non-slip rubber sheet to cover the exposed magnet face. See the
[magnet and non-slip covering notes](hardware/README.md#magnet-and-non-slip-covering).

Before printing, check the [printer settings and fit notes](hardware/README.md).
The included G-code is for a **Bambu P1S, 0.4 mm nozzle**; the 3MF uses an
**A1 mini** profile. Using a different printer? Slice the STL or 3MF for yours.

---

## 🌱 Contribute

New animations, cases, supported boards, and thoughtful fixes are welcome.
Humans and AI agents contribute through the same pull-request workflow.

[Work on firmware](firmware/README.md) · [Contribute a print](hardware/README.md#for-agents-and-contributors) · [Add a board](firmware/README.md#add-a-board)

<details>
<summary><strong>Contributor notes & repository map</strong></summary>

Fork, create a branch, make a focused change, and open a pull request. Read the
firmware guide before editing. For firmware or tooling changes, run
`python tools/check.py` and `python -m unittest discover -s tests -v`, then build
both original S3 boards and the C6 target with their respective SDKs; see
[the build environments](FORK.md#targets-and-build-environments). State which
physical board you tested; a build alone is not a hardware test.

Keep existing USB/Bluetooth/Wi-Fi commands compatible. New boards need their
own configuration, asset pack, and physical test evidence. Keep shared behavior
shared, preserve third-party credits, and never contribute device secrets,
factory records, private keys, or personal logs.

- `firmware/` — ESP-IDF source and board configurations.
- `assets/` — animation and icon packs used by the firmware.
- `hardware/` — printable cases and other physical parts.
- `tools/` — USB setup, installation, builds, and integrity checks.
- `tests/` — checks that don't touch physical hardware.

</details>

---

Code and original mechanical designs are **Apache-2.0**. Taby artwork has its
own [license terms](LICENSE): use it in your personal projects and keep it Taby;
don't rebrand the face or animations as your own different character or product.
[Third-party credits](firmware/NOTICE).

<p align="center">A little more personality on your desk. <a href="https://www.heytaby.com">Hey Taby ↗</a></p>
