"""Exercise the actual portable C builders at the desktop BLE wire boundary."""
import ctypes as C
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'firmware/targets/c6-lcd-1.47/main'


class BleV2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        assert (MAIN / 'c6_ble_v2.c').exists(), 'BLE v2 wire builders are not implemented'
        cls.tmp = tempfile.TemporaryDirectory()
        lib = Path(cls.tmp.name) / 'ble_v2.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared',
                        '-fPIC', str(MAIN / 'c6_ble_v2.c'), '-o', str(lib)], check=True)
        cls.lib = C.CDLL(str(lib))
        cls.identity = cls.lib.c6_ble_v2_identity
        cls.identity.argtypes = [C.c_void_p, C.c_size_t, C.c_char_p, C.c_char_p]
        cls.choice = cls.lib.c6_ble_v2_choice
        cls.choice.argtypes = [C.c_void_p, C.c_size_t, C.c_uint32, C.c_char_p]
        cls.orientation = cls.lib.c6_ble_v2_orientation
        cls.orientation.argtypes = [C.c_void_p, C.c_size_t, C.c_uint16,
                                   C.c_char_p, C.c_char_p, C.c_bool]
        for fn in (cls.identity, cls.choice, cls.orientation):
            fn.restype = C.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_identity_is_strict_raw_json_with_truthful_fields(self):
        out = C.create_string_buffer(256)
        self.assertTrue(self.identity(out, len(out), b'c6-local:012345abcdef', b'0.2.0-c6-dev.2'))
        self.assertEqual(json.loads(out.value), {
            'protocol': 2, 'deviceId': 'c6-local:012345abcdef',
            'firmwareVersion': '0.2.0-c6-dev.2'})
        self.assertTrue(self.identity(out, len(out), b'A' * 120, b'v' * 80))
        self.assertEqual(len(json.loads(out.value)['deviceId']), 120)
        self.assertEqual(len(json.loads(out.value)['firmwareVersion']), 80)

    def test_identity_rejects_empty_oversize_and_json_injection(self):
        out = C.create_string_buffer(256)
        for device in [None, b'', b'x' * 121, b'x y', b'x"', b'x\\', b'x\n', b'\xc3\xa4']:
            self.assertFalse(self.identity(out, len(out), device, b'v1'))
            self.assertEqual(out.value, b'')
        for version in [None, b'', b'x' * 81, b'x"', b'x\\', b'x\n', b'x\x7f']:
            self.assertFalse(self.identity(out, len(out), b'local', version))
            self.assertEqual(out.value, b'')

    def test_choice_maps_owner_selection_without_resetting_signal(self):
        out = C.create_string_buffer(80)
        for signal, selection, expected in [
            (0, b'none', {'signal': 0, 'choice': 'none'}),
            (1, b'choice_1', {'signal': 1, 'choice': 'left'}),
            (2, b'choice_2', {'signal': 2, 'choice': 'right'}),
            (0xffffffff, b'none', {'signal': 4294967295, 'choice': 'none'}),
            (42, b'other', {'signal': 42, 'choice': 'none'}),
        ]:
            self.assertTrue(self.choice(out, len(out), signal, selection))
            self.assertEqual(json.loads(out.value), expected)
        self.assertFalse(self.choice(out, len(out), 0, None))

    def test_orientation_uses_exact_native_fixed_and_auto_grammar(self):
        out = C.create_string_buffer(128)
        for degrees, name in [(0, b'left'), (90, b'90'), (180, b'right'), (270, b'270')]:
            self.assertTrue(self.orientation(out, len(out), degrees, name, name, False))
            self.assertEqual(out.value, b'OK DISPLAY_ORIENTATION ' + str(degrees).encode()
                             + b' ' + name + b' ' + name + b' 0 rectangle')
            self.assertTrue(self.orientation(out, len(out), degrees, b'auto', name, True))
            self.assertEqual(out.value, b'OK DISPLAY_ORIENTATION ' + str(degrees).encode()
                             + b' auto ' + name + b' 1 rectangle')

    def test_orientation_rejects_noncanonical_or_inconsistent_fields(self):
        out = C.create_string_buffer(128)
        for args in [(0, b'rotate_90', b'left'), (90, b'auto', b'rotate_90'),
                     (360, b'auto', b'left'), (0, b'left', b'right'),
                     (0, b'right', b'left'), (0, None, b'left'),
                     (0, b'left', None), (0, b'auto\nOK', b'left')]:
            self.assertFalse(self.orientation(out, len(out), *args, True))
            self.assertEqual(out.value, b'')

    def test_exact_fit_short_buffers_and_guard_bytes(self):
        for fn, args in [(self.identity, (b'c6.local', b'v1')),
                         (self.choice, (0xffffffff, b'choice_2')),
                         (self.orientation, (270, b'auto', b'270', True))]:
            full = C.create_string_buffer(256)
            self.assertTrue(fn(full, 256, *args))
            size = len(full.value) + 1
            for capacity in [0, 1, size - 1, size]:
                raw = (C.c_ubyte * (size + 2))(*([0xa5] * (size + 2)))
                ptr = C.cast(C.byref(raw, 1), C.c_void_p)
                self.assertEqual(fn(ptr, capacity, *args), capacity == size)
                self.assertEqual(raw[0], 0xa5)
                self.assertEqual(raw[capacity + 1], 0xa5)
                if capacity and capacity < size:
                    self.assertEqual(raw[1], 0)
            self.assertFalse(fn(None, 256, *args))


if __name__ == '__main__':
    unittest.main()
