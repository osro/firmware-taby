"""Catch frame corruption and pack encoder/firmware format disagreement."""
import ctypes
import importlib.util
from pathlib import Path
import random
import subprocess
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CODE = ROOT / 'firmware/targets/c6-lcd-1.47/main'


class C6CodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        lib = Path(cls.temp.name) / 'codec.dylib'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', str(CODE / 'c6_codec.c'), '-o', str(lib)], check=True)
        cls.decode = ctypes.CDLL(str(lib)).c6_rle_decode
        cls.decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                              ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        cls.decode.restype = ctypes.c_bool
        spec = importlib.util.spec_from_file_location('c6_assets', ROOT / 'tools/c6_assets.py')
        cls.encoder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.encoder)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def unpack(self, packed, count):
        palette = bytes(v for i in range(64) for v in (i, 255-i))
        output = ctypes.create_string_buffer(count * 2)
        valid = self.decode(packed, len(packed), palette, output, count)
        return valid, output.raw

    def test_repeat_and_literal_pixels_keep_palette_byte_order(self):
        self.assertEqual(self.unpack(bytes([0x82, 1, 1, 2, 3]), 5),
                         (True, bytes([1,254,1,254,1,254,2,253,3,252])))

    def test_rejects_truncated_overlong_and_invalid_palette_data(self):
        for data, count in [(b'\x82', 3), (b'\x01\x02', 2),
                            (b'\x83\x01', 3), (b'\x80\x40', 1),
                            (b'\x00\x01\x00\x02', 1), (b'', 1)]:
            with self.subTest(data=data):
                self.assertFalse(self.unpack(data, count)[0])

    def test_python_encoder_roundtrips_through_actual_firmware_decoder(self):
        rng = random.Random(147)
        for pixels in [bytes([0])*1000, bytes(range(64))*5,
                       bytes(rng.randrange(64) for _ in range(2000)),
                       bytes([1,2,2,3,3,3,4])*100]:
            valid, output = self.unpack(self.encoder.encode_rle(pixels), len(pixels))
            self.assertTrue(valid)
            self.assertEqual(output, bytes(v for i in pixels for v in (i,255-i)))

    def test_encoder_refuses_out_of_palette_values(self):
        with self.assertRaises(ValueError):
            self.encoder.encode_rle(b'\x40')

    @unittest.skipUnless(importlib.util.find_spec('PIL'), 'Install tools/requirements-c6.txt for image conversion tests')
    def test_single_color_frame_has_a_complete_palette_and_decodes(self):
        from PIL import Image
        data, _ = self.encoder.encode_image(Image.new('RGB', (10, 10), 'black'))
        output = ctypes.create_string_buffer(172*280*2)
        self.assertTrue(self.decode(data[128:], len(data)-128, data[:128], output, 172*280))
        self.assertEqual(output.raw, bytes(172*280*2))

    @unittest.skipUnless(importlib.util.find_spec('PIL'), 'Install tools/requirements-c6.txt for image conversion tests')
    def test_gif_conversion_preserves_total_time_and_nonloop_flag(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'tiny.gif'
            Image.new('RGB', (10, 10), 'red').save(
                path, save_all=True, append_images=[Image.new('RGB', (10, 10), 'blue')],
                duration=[80, 100])
            packed, _, durations = self.encoder.encode_gif(path, False)
        self.assertEqual(struct.unpack('<4sHHHH', packed[:12]), (b'TC6A',172,280,2,0))
        self.assertEqual(durations, [80,100])


if __name__ == '__main__':
    unittest.main()
