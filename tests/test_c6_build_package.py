"""Synthetic C6 build/package contract; no SDK build or hardware required."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))


class C6ToolingTests(unittest.TestCase):
    def setUp(self):
        import c6_build
        import c6_package
        self.builder, self.packager = c6_build, c6_package
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.target = self.root / 'firmware/targets/c6-lcd-1.47'
        self.build = self.root / 'firmware/build-c6-lcd-1.47'
        self.config = self.root / 'firmware/sdkconfig.c6-lcd-1.47'
        self.flash = self.root / 'dist/c6-lcd-1.47'
        self.sd = self.root / 'dist/c6-lcd-1.47-sd/taby'
        self.output = self.root / 'dist/package'
        self.write('LICENSE', 'artwork license')
        self.write('firmware/NOTICE', 'upstream notice')
        self.write('firmware/targets/c6-lcd-1.47/NOTICE', 'driver notice')
        self.write('firmware/targets/c6-lcd-1.47/README.md', 'Target guide\nSee [recorded verification](../../../STATUS.md).')
        self.write('firmware/targets/c6-lcd-1.47/CMakeLists.txt', 'set(PROJECT_VER "0.2.0")')
        self.write('firmware/targets/c6-lcd-1.47/main/c6_version.h', '#define C6_FIRMWARE_VERSION "0.2.0-c6-dev.99"')
        self.write('firmware/targets/c6-lcd-1.47/dependencies.lock', 'pinned')
        self.write('firmware/targets/c6-lcd-1.47/sdkconfig.defaults', 'defaults')
        self.write('firmware/main/shared.c', 'int shared;')
        self.write('assets/amoled-1.64/a.gif', b'original')
        self.write_json('assets/amoled-1.64/manifest.json', {'version': '0.4.1', 'animations': [{'id': 'a', 'relative_path': 'a.gif'}]})
        self.write_json('assets/amoled-1.64/catalog.json', {'animations': [{'id': 'a'}]})
        self.config.write_text('CONFIG_IDF_TARGET="esp32c6"\nCONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\n')
        self.write('firmware/targets/c6-lcd-1.47/partitions.csv', 'nvs,data,nvs,0x9000,0x6000,\nphy_init,data,phy,0xf000,0x1000,\nfactory,app,factory,0x10000,0x1f0000,\nassets,data,0x40,0x200000,0x600000,readonly\n')
        for directory, prefix in ((self.flash, 'C6_ASSET'), (self.sd, 'C6_SD_ASSET')):
            directory.mkdir(parents=True)
            pack = b'animation'
            (directory / 'assets.bin').write_bytes(pack)
            digest = hashlib.sha256(pack).hexdigest()
            (directory / 'assets.json').write_text(json.dumps({'board': 'c6-lcd-1.47', 'version': 'sd-test' if directory == self.sd else 'flash-test', 'width': 172, 'height': 280, 'bytes': len(pack), 'sha256': digest, 'crc32': zlib.crc32(pack), 'source_assets': '0.4.1', 'clips': [{'id': 'a', 'offset': 0, 'size': len(pack), 'source_sha256': hashlib.sha256(b'original').hexdigest()}]}))
            self.write(f'firmware/targets/c6-lcd-1.47/main/generated/{"c6_sd_catalog.h" if directory == self.sd else "c6_catalog.h"}', f'#define {prefix}_SHA256 "{digest}"\n#define {prefix}_BYTES {len(pack)}u\n#define {prefix}_CRC32 0x{zlib.crc32(pack):08x}u\n')
        self.write('dependency/LICENSE', 'dependency terms')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', '-C', str(self.root), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        self.produce_build()

    def write(self, name, value):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value if isinstance(value, bytes) else value.encode())

    def write_json(self, name, value):
        self.write(name, json.dumps(value))

    def produce_build(self):
        self.write('firmware/build-c6-lcd-1.47/bootloader/bootloader.bin', b'boot')
        self.write('firmware/build-c6-lcd-1.47/partition_table/partition-table.bin', b'partitions')
        self.write('firmware/build-c6-lcd-1.47/taby_c6.bin', b'application')
        self.write_json('firmware/build-c6-lcd-1.47/flasher_args.json', {'flash_files': {'0x0': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin', '0x10000': 'taby_c6.bin'}, 'flash_settings': {'flash_size': '8MB'}, 'extra_esptool_args': {'chip': 'esp32c6'}})
        self.write_json('firmware/build-c6-lcd-1.47/project_description.json', {'target': 'esp32c6', 'project_version': '0.2.0', 'git_revision': 'v5.5.2', 'config_file': str(self.config), 'project_path': str(self.target), 'build_component_paths': [str(self.root / 'dependency')]})
        self.write('firmware/build-c6-lcd-1.47/CMakeCache.txt', f'C6_SD_TRACE:BOOL=OFF\nCMAKE_HOME_DIRECTORY:INTERNAL={self.target}\nSDKCONFIG:FILEPATH={self.config}\n')

    def stamp(self):
        value = self.builder.build_fingerprint(self.root, self.build, self.config)
        (self.build / 'c6-build.json').write_text(json.dumps(value))

    def package(self):
        return self.packager.package(self.build, self.output, self.flash, self.sd, root=self.root)

    def test_package_contains_four_reviewed_images_notices_and_checked_sd_archive(self):
        self.stamp()
        self.package()
        manifest = json.loads((self.output / 'manifest.json').read_text())
        self.assertEqual(manifest['schema'], 'taby-c6-development-v2')
        self.assertTrue(manifest['experimental'])
        self.assertEqual(manifest['firmware_version'], '0.2.0-c6-dev.99')
        self.assertEqual([i['offset'] for i in manifest['images']], [0, 0x8000, 0x10000, 0x200000])
        for entry in manifest['images']:
            self.assertEqual(entry['sha256'], hashlib.sha256((self.output / entry['file']).read_bytes()).hexdigest())
        self.assertEqual((self.output / 'licenses/dependency/LICENSE').read_text(), 'dependency terms')
        self.assertEqual((self.output / 'LICENSE').read_text(), 'artwork license')
        self.assertIn('NVS', (self.output / 'INSTALL.txt').read_text())
        guide = (self.output / 'README.md').read_text()
        self.assertNotIn('](../../../STATUS.md)', guide)
        self.assertIn('source repository', guide)
        self.assertIn('STATUS.md', guide)
        for name in ('firmware.zip', 'sd.zip'):
            archive = self.output / name
            self.assertEqual((self.output / (name + '.sha256')).read_text().split()[0], hashlib.sha256(archive.read_bytes()).hexdigest())
            with zipfile.ZipFile(archive) as bundle:
                self.assertIsNone(bundle.testzip())
                if name == 'sd.zip':
                    self.assertEqual(set(bundle.namelist()), {'taby/assets.bin', 'taby/assets.json'})
        self.assertEqual(manifest['sd_assets_version'], 'sd-test')
        self.assertEqual(manifest['sd_archive_sha256'], hashlib.sha256((self.output / 'sd.zip').read_bytes()).hexdigest())

    def test_rejects_stale_inputs_and_preserves_existing_output(self):
        for name in ('firmware/main/shared.c', 'firmware/targets/c6-lcd-1.47/main/generated/c6_catalog.h', 'firmware/targets/c6-lcd-1.47/dependencies.lock', 'firmware/sdkconfig.c6-lcd-1.47', 'firmware/build-c6-lcd-1.47/taby_c6.bin', 'firmware/build-c6-lcd-1.47/flasher_args.json'):
            with self.subTest(name=name):
                self.stamp()
                path = self.root / name
                before = path.read_bytes()
                path.write_bytes(before + b' ')
                with self.assertRaises((ValueError, OSError)):
                    self.package()
                self.assertFalse(self.output.exists())
                path.write_bytes(before)
        self.stamp()
        self.output.mkdir()
        (self.output / 'keep').write_text('previous')
        with self.assertRaises((ValueError, FileExistsError)):
            self.package()
        self.assertEqual((self.output / 'keep').read_text(), 'previous')

    def test_rejects_wrong_target_sdk_version_trace_config_and_flash_map(self):
        cases = [('project_description.json', 'target', 'esp32s3'), ('project_description.json', 'git_revision', 'v5.4.2'), ('project_description.json', 'project_version', '9.0'), ('flasher_args.json', 'flash_files', {'0x0': 'taby_c6.bin'}), ('flasher_args.json', 'flash_files', {'0': 'bootloader/bootloader.bin', '0x0': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin', '0x10000': 'taby_c6.bin'})]
        for filename, key, bad in cases:
            with self.subTest(key=key, bad=bad):
                self.produce_build()
                path = self.build / filename
                data = json.loads(path.read_text()); data[key] = bad; path.write_text(json.dumps(data))
                self.stamp()
                with self.assertRaises(ValueError):
                    self.package()
        self.produce_build()
        for path, contents in ((self.config, 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y'), (self.build / 'CMakeCache.txt', 'C6_SD_TRACE:BOOL=ON')):
            before = path.read_text(); path.write_text(contents); self.stamp()
            with self.assertRaises(ValueError):
                self.package()
            path.write_text(before)

    def test_rejects_escaped_oversized_and_incomplete_builds(self):
        self.stamp()
        with self.assertRaises(ValueError):
            self.packager.package(self.build, self.root / 'outside', self.flash, self.sd, root=self.root)
        for escaped in ('../../LICENSE', str(self.root / 'LICENSE')):
            self.produce_build()
            path = self.build / 'flasher_args.json'; data = json.loads(path.read_text()); data['flash_files']['0x0'] = escaped; path.write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                self.stamp(); self.package()
        self.produce_build()
        (self.build / 'bootloader/bootloader.bin').write_bytes(b'x' * (0x8000 + 1)); self.stamp()
        with self.assertRaises(ValueError):
            self.package()
        self.produce_build(); self.stamp(); (self.build / 'partition_table/partition-table.bin').unlink()
        with self.assertRaises((ValueError, OSError)):
            self.package()
        self.assertFalse(self.output.exists())

    def test_rejects_tampered_flash_sd_metadata_and_unrelated_sd_files(self):
        self.stamp()
        for directory in (self.flash, self.sd):
            path = directory / 'assets.bin'; before = path.read_bytes(); path.write_bytes(b'bad')
            with self.assertRaises(ValueError):
                self.package()
            path.write_bytes(before)
            path = directory / 'assets.json'; before = path.read_text(); data = json.loads(before); data['clips'][0]['source_sha256'] = 'stale'; path.write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                self.package()
            path.write_text(before)
        (self.sd / 'private.txt').write_text('private')
        self.package()
        with zipfile.ZipFile(self.output / 'sd.zip') as bundle:
            self.assertNotIn('taby/private.txt', bundle.namelist())

    def test_build_stamp_binds_asset_metadata_even_when_pack_bytes_are_unchanged(self):
        self.stamp()
        path = self.flash / 'assets.json'
        data = json.loads(path.read_text()); data['version'] = 'unbuilt-version'; path.write_text(json.dumps(data))
        with self.assertRaises(ValueError):
            self.package()
        self.assertFalse(self.output.exists())

    def test_rejects_self_consistent_build_stamp_with_stale_asset_metadata_or_catalog(self):
        path = self.flash / 'assets.json'
        before = path.read_text()
        for field, bad in (('source_assets', 'old'), ('crc32', 1), ('clips', []), ('sha256', 'bad')):
            with self.subTest(field=field):
                data = json.loads(before); data[field] = bad; path.write_text(json.dumps(data)); self.stamp()
                with self.assertRaises(ValueError):
                    self.package()
        data = json.loads(before); data['clips'][0]['source_sha256'] = 'stale'
        path.write_text(json.dumps(data)); self.stamp()
        with self.assertRaises(ValueError):
            self.package()
        path.write_text(before)
        header = self.target / 'main/generated/c6_catalog.h'; header.write_text('wrong compiled header'); self.stamp()
        with self.assertRaises(ValueError):
            self.package()

    def test_asset_pack_accepts_storage_height_and_rejects_display_height(self):
        # tools/c6_assets.py stores 172x280 frames; the physical panel is 172x320.
        # Validate both real converter schema dimensions independently of display geometry.
        for directory, sd in ((self.flash, False), (self.sd, True)):
            with self.subTest(sd=sd):
                self.assertEqual(self.packager._asset_pack(self.root, directory, sd=sd)['height'], 280)
                path = directory / 'assets.json'
                before = path.read_text()
                data = json.loads(before); data['height'] = 320; path.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    self.packager._asset_pack(self.root, directory, sd=sd)
                path.write_text(before)

    def test_generated_catalog_accepts_leading_zero_crc_for_flash_and_sd(self):
        # Real converter uses eight hexadecimal digits: CRC32(b'33') = 0x0a6216d9.
        digest = hashlib.sha256(b'33').hexdigest()
        for directory, sd, prefix, header_name in (
                (self.flash, False, 'C6_ASSET', 'c6_catalog.h'),
                (self.sd, True, 'C6_SD_ASSET', 'c6_sd_catalog.h')):
            with self.subTest(sd=sd):
                (directory / 'assets.bin').write_bytes(b'33')
                path = directory / 'assets.json'; data = json.loads(path.read_text())
                data.update(bytes=2, crc32=0x0a6216d9, sha256=digest)
                data['clips'][0]['size'] = 2; path.write_text(json.dumps(data))
                header = self.target / 'main/generated' / header_name
                header.write_text(f'#define {prefix}_SHA256 "{digest}"\n#define {prefix}_BYTES 2u\n#define {prefix}_CRC32 0x0a6216d9u\n')
                self.assertEqual(self.packager._asset_pack(self.root, directory, sd=sd)['crc32'], 0x0a6216d9)
                header.write_text(header.read_text().replace('0x0a6216d9u', '0x0a6216d8u'))
                with self.assertRaises(ValueError):
                    self.packager._asset_pack(self.root, directory, sd=sd)

    def test_partition_changes_and_asset_path_escapes_are_rejected(self):
        path = self.target / 'partitions.csv'; before = path.read_text()
        path.write_text(before.replace('0x1f0000', '0x2f0000')); self.stamp()
        with self.assertRaises(ValueError):
            self.package()
        path.write_text(before)
        path = self.flash / 'assets.bin'; path.unlink(); path.symlink_to(self.root / 'LICENSE')
        with self.assertRaises(ValueError):
            self.stamp(); self.package()

    def test_builder_requires_both_generated_packs_before_invoking_build(self):
        idf = self.root / 'sdk/tools/idf.py'; idf.parent.mkdir(parents=True); idf.write_text('')
        (self.sd / 'assets.json').unlink()
        with patch.dict(os.environ, {'IDF_PATH': str(idf.parents[1])}), patch.object(self.builder.subprocess, 'check_output', return_value='ESP-IDF v5.5.2'):
            with patch.object(self.builder.subprocess, 'run', side_effect=AssertionError('build should not start')):
                with self.assertRaises((ValueError, FileNotFoundError)):
                    self.builder.build(self.root, self.build, self.config)

    def test_source_fingerprint_ignores_installed_dependencies_caches_and_private_paths(self):
        before = self.builder.source_digest(self.root)
        for name in ('firmware/targets/c6-lcd-1.47/managed_components/lvgl/new.c', 'firmware/build-other/generated.c', '.local/private.key', 'firmware/targets/c6-lcd-1.47/__pycache__/cache.pyc'):
            self.write(name, 'ignored')
        self.assertEqual(before, self.builder.source_digest(self.root))
        self.write('firmware/targets/c6-lcd-1.47/components/driver.c', 'new source')
        self.assertNotEqual(before, self.builder.source_digest(self.root))

    def test_builder_rejects_wrong_sdk_and_failed_or_mutating_build_without_stamp(self):
        idf = self.root / 'sdk/tools/idf.py'; idf.parent.mkdir(parents=True); idf.write_text('')
        with patch.dict(os.environ, {'IDF_PATH': str(idf.parents[1])}):
            with patch.object(self.builder.subprocess, 'check_output', return_value='ESP-IDF v5.4.2'):
                with self.assertRaises(ValueError):
                    self.builder.build(self.root, self.build, self.config)
            self.assertFalse((self.build / 'c6-build.json').exists())
            with patch.object(self.builder.subprocess, 'check_output', return_value='ESP-IDF v5.5.2'):
                with patch.object(self.builder.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'idf')):
                    with self.assertRaises(subprocess.CalledProcessError):
                        self.builder.build(self.root, self.build, self.config)
                self.assertFalse((self.build / 'c6-build.json').exists())
                def mutate(*args, **kwargs):
                    self.write('firmware/main/shared.c', 'changed during build')
                with patch.object(self.builder.subprocess, 'run', side_effect=mutate):
                    with self.assertRaises(ValueError):
                        self.builder.build(self.root, self.build, self.config)
                self.assertFalse((self.build / 'c6-build.json').exists())

    def test_successful_builder_stamps_only_completed_build(self):
        idf = self.root / 'sdk/tools/idf.py'; idf.parent.mkdir(parents=True); idf.write_text('')
        def installed(*args, **kwargs):
            self.write('firmware/targets/c6-lcd-1.47/managed_components/library/header.h', 'installed')
        with patch.dict(os.environ, {'IDF_PATH': str(idf.parents[1])}), patch.object(self.builder.subprocess, 'check_output', return_value='ESP-IDF v5.5.2'), patch.object(self.builder.subprocess, 'run', side_effect=installed):
            self.builder.build(self.root, self.build, self.config)
        self.assertEqual(json.loads((self.build / 'c6-build.json').read_text())['source_sha256'], self.builder.source_digest(self.root))
        self.package()
