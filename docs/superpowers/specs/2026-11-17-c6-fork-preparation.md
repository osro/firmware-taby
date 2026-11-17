# Local fork preparation

Owner requested: prepare the port as a fork of the original repository, without
pushing, and retain support for the other boards. All work stays local. Do not
create a GitHub repository/fork, create any PR or PR draft, push, release or
flash hardware.

The fork starts from the completed C6 development branch and merges current
upstream main `4e515faf63184f050d81b3f91421cb98a0f849d4` (firmware1.4.0).
Original S3 firmware/build/package paths must remain unchanged against that
upstream; preserve the amoled-1.64 and round-1.32 CI matrix on ESP-IDF5.4.2.
C6 remains a separate experimental target using ESP-IDF5.5.2 and four-image
layout, not an addition to the S3-only installer/OTA manifest contract.

The installed board stays on verified dev.5 with its unchanged0.2.0-sd card.
That source/binary/SD package and development branch are retained. The fork’s
unflashed dev.6 includes upstream asset catalog0.4.1, with new0.2.1-sd pack
CRC/catalog; only one upstream clip changes, and the sixteen-clip flash pack
is identical. Distinguish source/build compatibility from physical verification.

Provide reproducible C6 build and packaging entry points and an additive CI
job. Keep original tools/build.py, tools/package_release.py, tools/common.py,
firmware/main, firmware/components and original board metadata/targets unchanged.
Packaging must reject stale/mismatched inputs, wrong targets/layouts, escapes,
oversized images and accidental overwrites; include hashes and license notices.
No private flash backup, credentials, ignored app source, caches or local logs
may enter public source/export artifacts. Preserve LICENSE/NOTICE terms.

Prepare only the local fork branch/remotes and concise fork/build guide. Keep
original development history. The owner explicitly excluded any PR or PR draft;
do not prepare one. Retain checked local build/package outputs for validation,
but no additional Git bundle or patch export is requested. Clearly state that
no remote fork has been created and no upload occurred. Builds for both original
boards establish source/build compatibility; no physical S3 testing is possible
on the connected C6.
