"""Package only validated experimental C6 build outputs; no device or release action."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile
import zlib

from common import ROOT, inside, sha256
from c6_build import BOARD, TARGET, DEFAULT_BUILD, DEFAULT_CONFIG, build_fingerprint, _image_path
from package_release import copy_dependency_notices

LAYOUT = {'bootloader': (0, 0x8000), 'partition-table': (0x8000, 0x1000),
          'firmware': (0x10000, 0x1f0000), 'assets': (0x200000, 0x600000)}


def _asset_pack(root, directory, sd=False):
    """Validate generated pack bytes against source metadata and compiled catalog."""
    pack, metadata = inside(directory, 'assets.bin'), inside(directory, 'assets.json')
    data = json.loads(metadata.read_text())
    source = root / 'assets/amoled-1.64'
    original = json.loads((source / 'manifest.json').read_text())
    if (data.get('board') != BOARD or not data.get('version') or
            data.get('source_assets') != original['version'] or
            (data.get('width'), data.get('height')) != (172, 320) or
            data.get('bytes') != pack.stat().st_size or not pack.stat().st_size or
            data.get('sha256') != sha256(pack) or data.get('crc32') != zlib.crc32(pack.read_bytes())):
        raise ValueError('Stale or corrupt asset metadata/pack')
    prefix = 'C6_SD_ASSET' if sd else 'C6_ASSET'
    header = (root / TARGET / 'main/generated' / ('c6_sd_catalog.h' if sd else 'c6_catalog.h')).read_text()
    for suffix, expected in (('SHA256', f'"{data["sha256"]}"'), ('BYTES', str(data['bytes'])), ('CRC32', hex(data['crc32']))):
        match = re.search(rf'^#define {prefix}_{suffix}\s+(\S+)', header, re.M)
        if not match or match[1].rstrip('uU').lower() != expected.lower():
            raise ValueError('Asset metadata differs from compiled generated catalog')
    originals = {entry['id']: entry for entry in original['animations']}
    clips = data.get('clips', [])
    seen, end = set(), 0
    for clip in clips:
        name = clip['id']
        if name in seen or name not in originals:
            raise ValueError('Invalid asset catalog')
        seen.add(name)
        if clip.get('source_sha256') != sha256(inside(source, originals[name]['relative_path'])):
            raise ValueError('Asset source changed; regenerate assets')
        if type(clip.get('offset')) is not int or type(clip.get('size')) is not int or clip['offset'] < end or clip['offset'] % 4 or clip['size'] <= 0:
            raise ValueError('Invalid asset clip bounds')
        end = clip['offset'] + clip['size']
        if end > data['bytes']:
            raise ValueError('Asset clip exceeds pack')
    if not clips or (sd and seen != set(originals)):
        raise ValueError('Incomplete asset catalog')
    return data


def _zip_checked(directory, paths, archive):
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for path, name in paths:
            bundle.write(path, name)
    with zipfile.ZipFile(archive) as bundle:
        if bundle.testzip() is not None:
            raise ValueError('ZIP verification failed')
    digest = sha256(archive)
    (directory / (archive.name + '.sha256')).write_text(f'{digest}  {archive.name}\n')
    return digest


def _validated_build(root, build, config, assets=None, sd_assets=None):
    stamp = json.loads((build / 'c6-build.json').read_text())
    if stamp != build_fingerprint(root, build, config, assets, sd_assets):
        raise ValueError('Source, config, dependencies or outputs changed; run tools/c6_build.py again')
    description = json.loads((build / 'project_description.json').read_text())
    flash = json.loads((build / 'flasher_args.json').read_text())
    if description.get('target') != 'esp32c6' or description.get('git_revision') != 'v5.5.2':
        raise ValueError('Expected ESP32-C6 on ESP-IDF v5.5.2')
    if Path(description.get('config_file', '')).resolve() != config or Path(description.get('project_path', '')).resolve() != root / TARGET:
        raise ValueError('Build project/config path differs from selected target')
    config_text = config.read_text()
    if not re.search(r'^CONFIG_IDF_TARGET="esp32c6"$', config_text, re.M) or not re.search(r'^CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y$', config_text, re.M):
        raise ValueError('Expected esp32c6 8MB sdkconfig')
    cache = (build / 'CMakeCache.txt').read_text()
    for key, value in (('C6_SD_TRACE', 'OFF'), ('CMAKE_HOME_DIRECTORY', str(root / TARGET)), ('SDKCONFIG', str(config))):
        if not re.search(rf'^{key}:[^=]+={re.escape(value)}$', cache, re.M):
            raise ValueError(f'Unexpected build configuration: {key}')
    base = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)', (root / TARGET / 'CMakeLists.txt').read_text())
    full = re.search(r'^#define C6_FIRMWARE_VERSION\s+"([^"]+)"', (root / TARGET / 'main/c6_version.h').read_text(), re.M)
    if not base or not full or description.get('project_version') != base[1] or not full[1].startswith(base[1] + '-c6-'):
        raise ValueError('Project and C6 firmware version disagree')
    # Keep NVS/PHY untouched; only the reviewed application/assets map is supported.
    partitions = {}
    for line in (root / TARGET / 'partitions.csv').read_text().splitlines():
        if line.strip() and not line.lstrip().startswith('#'):
            name, kind, subtype, offset, size, *_ = [field.strip() for field in line.split(',')]
            partitions[name] = (kind, subtype, int(offset, 0), int(size, 0))
    if partitions != {'nvs': ('data', 'nvs', 0x9000, 0x6000), 'phy_init': ('data', 'phy', 0xf000, 0x1000),
                      'factory': ('app', 'factory', 0x10000, 0x1f0000), 'assets': ('data', '0x40', 0x200000, 0x600000)}:
        raise ValueError('Partition layout differs from reviewed C6 regions')
    actual = {int(offset, 0): name for offset, name in flash['flash_files'].items()}
    if len(flash['flash_files']) != 3 or set(actual) != {0, 0x8000, 0x10000}:
        raise ValueError('SDK flash map differs from reviewed C6 layout')
    if flash.get('flash_settings', {}).get('flash_size') != '8MB' or flash.get('extra_esptool_args', {}).get('chip') != 'esp32c6':
        raise ValueError('Wrong SDK flash size/chip')
    paths = [_image_path(build, name) for name in actual.values()]
    if len(set(paths)) != 3:
        raise ValueError('Flash map reuses an image')
    return stamp, description, actual, full[1]


def package(build, output, assets=None, sd_assets=None, *, root=ROOT, config=None):
    root, build, output = Path(root).resolve(), Path(build).resolve(), Path(output).resolve()
    assets = Path(assets or root / f'dist/{BOARD}').resolve()
    sd_assets = Path(sd_assets or root / f'dist/{BOARD}-sd/taby').resolve()
    config = Path(config or root / f'firmware/sdkconfig.{BOARD}').resolve()
    if not output.is_relative_to(root / 'dist') or output == root / 'dist':
        raise ValueError('Output must be a fresh directory beneath dist')
    if output.exists():
        raise FileExistsError(f'Refusing to overwrite {output}')
    for directory, base in ((build, root / 'firmware'), (assets, root / 'dist'), (sd_assets, root / 'dist')):
        if not directory.is_relative_to(base) or output.is_relative_to(directory) or directory.is_relative_to(output):
            raise ValueError('Input/output path escapes or overlaps')
    if config.parent != root / 'firmware':
        raise ValueError('Config must be beneath firmware')
    stamp, description, actual, version = _validated_build(root, build, config, assets, sd_assets)
    flash_metadata = _asset_pack(root, assets)
    sd_metadata = _asset_pack(root, sd_assets, sd=True)
    images = []
    for kind, (offset, maximum) in LAYOUT.items():
        source = inside(assets, 'assets.bin') if kind == 'assets' else _image_path(build, actual[offset])
        size = source.stat().st_size
        if size <= 0 or size > maximum:
            raise ValueError(f'Empty or oversized {kind} image')
        images.append((source, {'kind': kind, 'file': kind + '.bin', 'offset': offset, 'size': size, 'sha256': sha256(source)}))
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=normal'], cwd=root, text=True).strip())
    manifest = {'schema': 'taby-c6-development-v2', 'experimental': True, 'board': BOARD,
                'chip': 'esp32c6', 'flash_size_mb': 8, 'sdk_version': '5.5.2', 'firmware_version': version,
                'assets_version': flash_metadata['version'], 'source_sha256': stamp['source_sha256'],
                'source_commit': commit, 'source_dirty': dirty, 'images': [entry for _, entry in images],
                'sd_assets_version': sd_metadata['version'], 'sd_assets_sha256': sd_metadata['sha256'], 'sd_archive': 'sd.zip',
                'limitations': ['Experimental C6 target; separate from the official S3 installer/OTA contract.',
                                'Build and package validation do not establish physical device verification.',
                                'See target guide and repository STATUS.md for supported behavior and known gaps.']}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.c6-package-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'package'; stage.mkdir()
        for source, entry in images:
            shutil.copyfile(source, stage / entry['file'])
            if sha256(stage / entry['file']) != entry['sha256']:
                raise ValueError('Image changed while staging')
        for name, source in (('LICENSE', root / 'LICENSE'), ('NOTICE', root / 'firmware/NOTICE'),
                             ('TARGET-NOTICE', root / TARGET / 'NOTICE'), ('README.md', root / TARGET / 'README.md'),
                             ('assets.json', assets / 'assets.json')):
            shutil.copyfile(source, stage / name)
        copy_dependency_notices(stage, description)
        manifest['sd_archive_sha256'] = _zip_checked(stage, [(inside(sd_assets, name), 'taby/' + name) for name in ('assets.bin', 'assets.json')], stage / 'sd.zip')
        with zipfile.ZipFile(stage / 'sd.zip') as archive:
            if hashlib.sha256(archive.read('taby/assets.bin')).hexdigest() != sd_metadata['sha256']:
                raise ValueError('SD assets changed while staging')
        (stage / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (stage / 'INSTALL.txt').write_text(
            'Experimental Waveshare ESP32-C6-Touch-LCD-1.47 / -M, 8MB only. Read README.md.\n'
            'Keep a private backup before installation. Use esptool 4.x from the tools environment:\n'
            'python -m esptool --chip esp32c6 --port PORT --baud 460800 --before default_reset --after hard_reset '
            'write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m '
            '0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 firmware.bin 0x200000 assets.bin\n'
            'These four writes preserve NVS and PHY calibration. Do not erase the whole chip or change eFuses.\n'
            'Verify sd.zip.sha256, then extract taby/ onto FAT32 microSD; existing card content is not part of this package.\n'
            'Use the matching SD version in manifest.json. Unplug until the display is dark, insert card, reconnect.\n')
        excluded = {'sd.zip', 'sd.zip.sha256'}
        _zip_checked(stage, [(path, path.relative_to(stage).as_posix()) for path in sorted(stage.rglob('*')) if path.is_file() and path.name not in excluded], stage / 'firmware.zip')
        # Check freshness again before the only publication operation.
        _validated_build(root, build, config, assets, sd_assets)
        _asset_pack(root, assets); _asset_pack(root, sd_assets, sd=True)
        if output.exists():
            raise FileExistsError(f'Refusing to overwrite {output}')
        stage.rename(output)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=DEFAULT_BUILD)
    parser.add_argument('--config', type=Path, default=DEFAULT_CONFIG)
    parser.add_argument('--assets', type=Path, default=ROOT / f'dist/{BOARD}')
    parser.add_argument('--sd-assets', type=Path, default=ROOT / f'dist/{BOARD}-sd/taby')
    parser.add_argument('--output', type=Path, required=True, help='Fresh output directory beneath dist')
    args = parser.parse_args()
    print(package(args.build, args.output, args.assets, args.sd_assets, config=args.config))
