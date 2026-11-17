# C6 fork

This fork's `main` adds the experimental Waveshare ESP32-C6-Touch-LCD-1.47/-M
target alongside the original boards. It retains the fork's pre-merge main
`e7051ac4bed179b4067abde8c36a9b9e9fa5ffe1` (firmware1.4.0 source, including
the later S3 Bluetooth mouth-animation fix).
The original S3 runtime, board definitions and build/package tools remain
unchanged against that revision. [STATUS.md](STATUS.md) owns verification
results and known limitations.

## Targets and build environments

| Target | SDK | Instructions |
| --- | --- | --- |
| `amoled-1.64` — ESP32-S3 AMOLED1.64 V1 | ESP-IDF5.4.2 | [Original build guide](firmware/README.md#build-from-source) |
| `round-1.32` — ESP32-S3 Touch AMOLED1.32 | ESP-IDF5.4.2 | [Original build guide](firmware/README.md#build-from-source) |
| `c6-lcd-1.47` — ESP32-C6 Touch LCD1.47/-M | ESP-IDF5.5.2 | [Experimental target guide](firmware/targets/c6-lcd-1.47/README.md) |

Keep the SDK Python environments separate from the small tools `.venv`.
The original board matrix remains on5.4.2; the additive C6 CI job uses5.5.2.
Local workflow checks are recorded in STATUS; remote Actions results have not
been verified.

For C6, first generate matching assets from the repository root:

```sh
.venv/bin/python -m pip install -r tools/requirements-c6.txt
.venv/bin/python tools/c6_assets.py
.venv/bin/python tools/c6_assets.py --sd-card
```

Then, in an ESP-IDF5.5.2 shell, use its Python for the build:

```sh
python tools/c6_build.py
```

Package locally with the tools environment:

```sh
.venv/bin/python tools/c6_package.py --output dist/c6-development
```

The output must be a new directory. The builder stamps the source/configuration/
dependency/image fingerprint; packaging rejects changed or mismatched inputs.
The output includes the four-image firmware ZIP, a separate full-SD ZIP,
checksums, development manifest, installation guide and license notices.
These C6 images use a different layout and manifest from the original S3
installer/OTA bundles; follow the C6 guide for installation.

## Installed device versus fork source

The connected C6 remains on physically verified `0.2.0-c6-dev.5`, with its
existing `0.2.0-sd` card. Branch `codex/c6-lcd-147-verified-dev5` and the checked
local dev.5 binary/source/SD archives preserve that state.

Fork source is `0.2.0-c6-dev.6`. Upstream changed one animation in catalog0.4.1,
so its matching full SD pack is `0.2.1-sd`; regenerate/copy that matching pack
when deliberately installing the fork build. The sixteen-clip flash pack is
unchanged. Fork preparation has not flashed the device or written its card.
Physical C6 results apply to dev.5; source/build checks for dev.6 are recorded
separately. Neither original S3 board has been physically tested here.

C6 retains its documented limits: simplified live mouth art, no Rive body/eye
pose parity, unsupported embedded card-clip behavior, unverified battery-only
operation, and no established official cloud enrollment or configured MCP
grants. Bluetooth eye-mode controls are limited by the installed desktop app.
The original boards retain upstream’s own capabilities.

## Git state

- Working branch: `main`, tracking `origin/main`.
- `upstream`: original `TRIIIS-LABS/firmware-taby`; local upstream push URL is
  disabled.
- `origin`: [osro/firmware-taby](https://github.com/osro/firmware-taby).
- Installation source: [main](https://github.com/osro/firmware-taby/tree/main).
- The owner authorized merging `codex/c6-lcd-147-fork` into the fork's `main`
  after the feature branch was published. The feature branch remains preserved.
- Development history and the verified dev.5 branch are preserved.
- No PR, PR draft or release was created.

The build/package commands above remain local operations. They do not publish
anything or install firmware on a connected board. For first-time C6 setup,
start with the [README guide](README.md#c6-setup).
