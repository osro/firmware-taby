"""Build the experimental C6 target in an existing ESP-IDF v5.5.2 shell.

Generate flash and SD assets first using tools/c6_assets.py in the tools .venv.
This entry point deliberately uses the SDK shell's Python, never the tools venv.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from common import ROOT, inside, sha256

BOARD = 'c6-lcd-1.47'
TARGET = Path('firmware/targets') / BOARD
DEFAULT_BUILD = ROOT / f'firmware/build-{BOARD}'
DEFAULT_CONFIG = ROOT / f'firmware/sdkconfig.{BOARD}'


def source_digest(root=ROOT):
    """Hash real firmware/artwork, including ignored generated C6 headers.

    Prune installed dependencies and build/private/cache trees before walking:
    a first IDF dependency install must not change the source fingerprint.
    Lockfiles are bound separately after successful IDF resolution.
    """
    root = Path(root).resolve()
    paths = []
    for base in ('assets', 'firmware'):
        for current, directories, names in os.walk(root / base, followlinks=False):
            directories[:] = sorted(d for d in directories if not (
                d.startswith(('.', 'build')) or d in ('managed_components', '__pycache__', 'third_party', 'factory')))
            for name in names:
                if name.startswith(('.', 'sdkconfig.')) and name != 'sdkconfig.defaults':
                    continue
                if name in ('sdkconfig', 'dependencies.lock') or name.endswith(('.pyc', '.key', '.pem', '.log', '.old')):
                    continue
                path = Path(current) / name
                # Documentation does not affect compilation and can be edited in parallel.
                if base == 'firmware' and path.suffix.lower() in ('.md', '.rst', '.txt') and name != 'CMakeLists.txt':
                    continue
                if path.is_symlink() or not path.resolve().is_relative_to(root):
                    raise ValueError(f'Source symlink is unsupported: {path}')
                paths.append(path)
    paths += [root / 'tools' / name for name in ('c6_assets.py', 'c6_ui_icons.py') if (root / 'tools' / name).is_file()]
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(root).as_posix().encode() + b'\0')
        digest.update(sha256(path).encode() + b'\0')
    return digest.hexdigest()


def build_fingerprint(root, build, config, assets=None, sd_assets=None):
    root, build, config = Path(root), Path(build), Path(config)
    assets = Path(assets or root / f'dist/{BOARD}')
    sd_assets = Path(sd_assets or root / f'dist/{BOARD}-sd/taby')
    flash = json.loads((build / 'flasher_args.json').read_text())
    return {
        'schema': 'taby-c6-build-v1', 'board': BOARD,
        'source_sha256': source_digest(root),
        'config_sha256': sha256(config),
        'assets': {kind: {name: sha256(inside(directory, name)) for name in ('assets.bin', 'assets.json')}
                   for kind, directory in (('flash', assets), ('sd', sd_assets))},
        'dependencies_sha256': sha256(root / TARGET / 'dependencies.lock'),
        'build_metadata': {name: sha256(build / name) for name in
                           ('flasher_args.json', 'project_description.json', 'CMakeCache.txt')},
        'images': {name: sha256(_image_path(build, name)) for name in flash['flash_files'].values()},
    }


def _image_path(build, name):
    if Path(name).is_absolute():
        raise ValueError('Flash image paths must be relative to the build')
    return inside(build, name)


def build(root=ROOT, build_dir=DEFAULT_BUILD, config=DEFAULT_CONFIG):
    root, build_dir, config = Path(root).resolve(), Path(build_dir).resolve(), Path(config).resolve()
    target = root / TARGET
    if not build_dir.is_relative_to(root / 'firmware') or build_dir.is_relative_to(root / 'firmware/targets'):
        raise ValueError('Build directory must be under firmware and outside targets')
    if config.parent != root / 'firmware' or not config.name.startswith('sdkconfig.'):
        raise ValueError('Configuration must be a firmware/sdkconfig.* file outside targets')
    # Invalidate a previous successful stamp even if a later attempt fails early.
    stamp = build_dir / 'c6-build.json'
    stamp.unlink(missing_ok=True)
    idf = Path(os.environ.get('IDF_PATH', '')) / 'tools/idf.py'
    if not idf.is_file():
        raise ValueError('Open an ESP-IDF v5.5.2 shell first')
    version = subprocess.check_output([sys.executable, str(idf), '--version'], text=True)
    if not re.search(r'\bv5\.5\.2(?:\s|$)', version):
        raise ValueError(f'Expected ESP-IDF v5.5.2; got {version.strip()}')
    for name in ('c6_catalog.h', 'c6_sd_catalog.h'):
        if not (target / 'main/generated' / name).is_file():
            raise ValueError('Generate flash and full SD assets first with tools/c6_assets.py using the tools .venv')
    for directory in (root / f'dist/{BOARD}', root / f'dist/{BOARD}-sd/taby'):
        for name in ('assets.bin', 'assets.json'):
            if not inside(directory, name).is_file():
                raise ValueError('Generate flash and full SD assets first with tools/c6_assets.py using the tools .venv')
    before = source_digest(root)
    subprocess.run([sys.executable, str(idf), '-C', str(target), '-B', str(build_dir),
                    '-D', f'SDKCONFIG={config}', '-D', f'SDKCONFIG_DEFAULTS={target / "sdkconfig.defaults"}',
                    '-D', 'IDF_TARGET=esp32c6', '-D', 'C6_SD_TRACE=OFF', 'build'], cwd=root, check=True)
    if before != source_digest(root):
        raise ValueError('Source/assets changed during build; rebuild before packaging')
    fingerprint = build_fingerprint(root, build_dir, config)
    stamp.write_text(json.dumps(fingerprint, indent=2) + '\n')
    return stamp


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=DEFAULT_BUILD)
    parser.add_argument('--config', type=Path, default=DEFAULT_CONFIG)
    args = parser.parse_args()
    print(build(build_dir=args.build, config=args.config))
