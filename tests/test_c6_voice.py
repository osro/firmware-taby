"""Execute the upstream voice parser and bounded C6 mouth overlay on the host."""
import ctypes
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'firmware/targets/c6-lcd-1.47/main'


class Snapshot(ctypes.Structure):
    _fields_ = [('active', ctypes.c_bool), ('viseme', ctypes.c_uint8),
                ('jaw_open', ctypes.c_float), ('gesture', ctypes.c_uint8),
                ('expression', ctypes.c_uint8)]


class VoiceTests(unittest.TestCase):
    temp = None
    lib = None

    @classmethod
    def tearDownClass(cls):
        if cls.temp:
            cls.temp.cleanup()

    def setUp(self):
        # An absent implementation is an explicit failing assertion in the red run.
        self.assertTrue((MAIN / 'c6_voice.cpp').is_file(), 'C6 voice wrapper is missing')
        if VoiceTests.lib is None:
            VoiceTests.temp = tempfile.TemporaryDirectory()
            build = Path(VoiceTests.temp.name)
            # Same COPYONLY layout required in CMake: quoted include resolves locally.
            shutil.copyfile(ROOT / 'firmware/components/taby_character/taby_voice.cpp',
                            build / 'taby_voice.cpp')
            shutil.copyfile(MAIN / 'c6_voice_compat/taby_voice.hpp', build / 'taby_voice.hpp')
            library = build / 'voice.dylib'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-shared', '-fPIC', '-I', str(MAIN), '-I', str(build),
                            str(build / 'taby_voice.cpp'), str(MAIN / 'c6_voice.cpp'),
                            '-o', str(library)], check=True)
            lib = ctypes.CDLL(str(library))
            lib.c6_voice_command.argtypes = [ctypes.c_char_p, ctypes.c_double]
            lib.c6_voice_command.restype = ctypes.c_bool
            lib.c6_voice_active.argtypes = [ctypes.c_double]
            lib.c6_voice_active.restype = ctypes.c_bool
            lib.c6_voice_snapshot.argtypes = [ctypes.c_double, ctypes.POINTER(Snapshot)]
            lib.c6_voice_snapshot.restype = ctypes.c_bool
            lib.c6_voice_render.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                                            ctypes.c_uint16, ctypes.c_uint16, ctypes.c_double]
            lib.c6_voice_render.restype = ctypes.c_bool
            VoiceTests.lib = lib
        self.lib.c6_voice_reset()

    def frame(self, now):
        out = Snapshot()
        self.assertTrue(self.lib.c6_voice_snapshot(now, ctypes.byref(out)))
        return out

    def test_30fps_frame_boundary_and_jaw_endpoints(self):
        self.assertTrue(self.lib.c6_voice_command(b'9 0 0 AAAK_A', 1000))
        self.assertEqual((self.frame(1033).viseme, self.frame(1033).jaw_open), (0, 0))
        self.assertEqual((self.frame(1034).viseme, self.frame(1034).jaw_open), (10, 100))
        self.assertTrue(self.frame(1066).active)
        self.assertFalse(self.frame(1067).active)
        self.assertEqual(self.frame(1067).viseme, 0)

    def test_all_visemes_jaw_and_pose_values_survive_parser(self):
        for v in range(15):
            for pose in range(28):
                letters = b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'
                cmd = b'7 0 0 ' + bytes((letters[v], letters[31], letters[pose]))
                self.assertTrue(self.lib.c6_voice_command(cmd, 0))
                out = self.frame(1)
                self.assertEqual((out.viseme, out.gesture, out.expression), (v, pose // 4, pose % 4))
                self.assertAlmostEqual(out.jaw_open, 100 * 31 / 63, places=4)

    def test_gap_is_silence_and_future_frame_keeps_voice_active(self):
        self.assertTrue(self.lib.c6_voice_command(b'7 0 100 K_A', 0))
        self.assertTrue(self.frame(50).active)
        self.assertEqual(self.frame(50).viseme, 0)
        self.assertEqual(self.frame(100).viseme, 10)

    def test_stop_only_stops_matching_talk_and_reset_forgets_frames(self):
        self.assertTrue(self.lib.c6_voice_command(b'7 0 0 K_A', 0))
        self.assertTrue(self.lib.c6_voice_command(b'8 stop', 1))
        self.assertTrue(self.lib.c6_voice_active(2))
        self.assertTrue(self.lib.c6_voice_command(b'7 stop', 3))
        self.assertFalse(self.lib.c6_voice_active(4))
        self.assertEqual(self.frame(4).jaw_open, 0)
        self.assertTrue(self.lib.c6_voice_command(b'7 0 100 O_A', 5))
        self.assertEqual(self.frame(5).viseme, 0)
        self.lib.c6_voice_reset()
        self.assertFalse(self.lib.c6_voice_active(5))

    def test_small_clock_drift_does_not_shift_clock_but_large_drift_does(self):
        self.assertTrue(self.lib.c6_voice_command(b'7 0 0 K_A', 1000))
        self.assertTrue(self.lib.c6_voice_command(b'7 0 100 O_A', 1080))
        self.assertEqual(self.frame(1100).viseme, 14)
        self.assertTrue(self.lib.c6_voice_command(b'7 0 100 N_A', 1081))
        self.assertEqual(self.frame(1100).viseme, 10)
        self.assertEqual(self.frame(1181).viseme, 13)

    def test_malformed_or_out_of_range_new_talk_preserves_current_voice(self):
        invalid = [None, b'', b'65536 0 0 K_A', b'-1 0 0 K_A', b'+1 0 0 K_A',
                   b'8 0 0 P_A', b'8 0 0 K_/', b'8 0 0 K_c', b'8 0 0 AA',
                   b'8 0 120000 K_A', b'8 0 119999 K_A', b'8 0 0 ' + b'K_A' * 61,
                   b'8 120001 0 K_A', b'8 0 999999999999999999999999 K_A',
                   b'7 0 120000 K_A', b'8 0 0 K_A ', b'8 0 0 K_A\n',
                   b'8  0 0 K_A', b'8\t0 0 K_A', b'8 0 0 ' + b'A' * 10000]
        for cmd in invalid:
            with self.subTest(cmd=str(cmd)[:65]):
                self.assertTrue(self.lib.c6_voice_command(b'7 0 0 K_A', 1000))
                self.assertFalse(self.lib.c6_voice_command(cmd, 1001))
                self.assertEqual((self.frame(1002).viseme, self.frame(1002).jaw_open), (10, 100))

    def test_last_frame_limit_accepts_3599_and_rejects_overrun_atomically(self):
        self.assertTrue(self.lib.c6_voice_command(b'7 119966 119967 O_A', 120000))
        self.assertEqual(self.frame(120001).viseme, 14)
        self.assertFalse(self.lib.c6_voice_command(b'8 0 119967 K_AK_A', 120001))
        self.assertEqual(self.frame(120002).viseme, 14)
        self.assertFalse(self.lib.c6_voice_active(120034))

    def test_finished_voice_is_silent_at_long_uptime(self):
        self.assertTrue(self.lib.c6_voice_command(b'7 0 0 K_A', 0))
        out = self.frame(1e12)
        self.assertEqual((out.active, out.viseme, out.jaw_open), (False, 0, 0))

    def test_overlay_clears_old_neutral_mouth_without_touching_eyes(self):
        count = 172 * 280 * 2
        original = bytearray(count)
        for x, y in [(50, 140), (52, 112), (57, 169), (89, 69), (89, 214)]:
            i = (y * 172 + x) * 2
            original[i:i+2] = b'\xFF\xFF'
        buf = (ctypes.c_uint8 * count).from_buffer_copy(original)
        self.assertTrue(self.lib.c6_voice_command(b'7 0 0 O_A', 0))
        self.assertTrue(self.lib.c6_voice_render(buf, count, 172, 280, 0))
        result = bytes(buf)
        for x, y in [(50, 140), (52, 112), (57, 169)]:
            i = (y * 172 + x) * 2
            self.assertEqual(result[i:i+2], b'\0\0')
        for x, y in [(89, 69), (89, 214)]:
            i = (y * 172 + x) * 2
            self.assertEqual(result[i:i+2], b'\xFF\xFF')

    def test_invalid_time_never_reaches_parser_or_frame_index_cast(self):
        self.assertTrue(self.lib.c6_voice_command(b'7 0 0 K_A', 0))
        for now in (-1, float('inf'), float('nan'), 1e30):
            self.assertFalse(self.lib.c6_voice_command(b'8 stop', now))
            self.assertFalse(self.lib.c6_voice_active(now))
            out = Snapshot()
            self.assertFalse(self.lib.c6_voice_snapshot(now, ctypes.byref(out)))
        self.assertFalse(self.lib.c6_voice_snapshot(0, None))
        self.assertEqual(self.frame(1).viseme, 10)

    def test_render_is_bounded_big_endian_and_draws_all_speaking_visemes(self):
        count = 172 * 280 * 2
        signatures = set()
        for v in range(1, 15):
            raw = (ctypes.c_uint8 * (count + 32))(*([0xA5] * 16 + [0] * count + [0xA5] * 16))
            pixels = ctypes.cast(ctypes.byref(raw, 16), ctypes.POINTER(ctypes.c_uint8))
            self.assertTrue(self.lib.c6_voice_command(b'7 0 0 ' + bytes((65 + v, 95, 65)), 0))
            self.assertTrue(self.lib.c6_voice_render(pixels, count, 172, 280, 0))
            result = bytes(raw)[16:-16]
            self.assertEqual(bytes(raw)[:16], b'\xA5' * 16)
            self.assertEqual(bytes(raw)[-16:], b'\xA5' * 16)
            changed = [(i // 2 % 172, i // 2 // 172) for i in range(0, count, 2) if result[i:i+2] != b'\0\0']
            self.assertTrue(changed)
            self.assertTrue(all(26 <= x <= 70 and 104 <= y <= 176 for x, y in changed))
            colors = {result[i:i+2] for i in range(0, count, 2)}
            self.assertIn(b'\xFF\xFF', colors)
            self.assertTrue(colors <= {b'\0\0', b'\xFF\xFF', b'\xAD\x55'})
            signatures.add(result)
        self.assertGreaterEqual(len(signatures), 10)

    def test_closed_jaw_changes_open_mouth_and_silence_preserves_neutral_face(self):
        count = 172 * 280 * 2
        buf = (ctypes.c_uint8 * count)()
        self.assertTrue(self.lib.c6_voice_command(b'7 0 0 K_A', 0))
        self.assertTrue(self.lib.c6_voice_render(buf, count, 172, 280, 0))
        opened = bytes(buf)
        ctypes.memset(buf, 0, count)
        self.assertTrue(self.lib.c6_voice_command(b'7 0 0 KAA', 0))
        self.assertTrue(self.lib.c6_voice_render(buf, count, 172, 280, 0))
        self.assertNotEqual(opened, bytes(buf))
        original = bytes([0x19]) * count
        for cmd, now in [(b'7 0 0 AAA', 0), (b'7 stop', 0)]:
            ctypes.memmove(buf, original, count)
            self.assertTrue(self.lib.c6_voice_command(cmd, 0))
            self.assertTrue(self.lib.c6_voice_render(buf, count, 172, 280, now))
            self.assertEqual(bytes(buf), original)

    def test_invalid_render_arguments_do_not_touch_buffer(self):
        buf = (ctypes.c_uint8 * 64)(*([0xA5] * 64))
        for size, width, height, now in [(64,172,280,0), (64,0,0,0),
                                         (64,65535,65535,0), (64,1,1,0),
                                         (64,172,280,float('nan'))]:
            self.assertFalse(self.lib.c6_voice_render(buf, size, width, height, now))
            self.assertEqual(bytes(buf), b'\xA5' * 64)
        self.assertFalse(self.lib.c6_voice_render(None, 96320, 172, 280, 0))


if __name__ == '__main__':
    unittest.main()
