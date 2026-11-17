"""The SD reader must reject truncated assets and clip/file boundary crossings."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class C6FileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        lib = Path(cls.temp.name)/'reader.dylib'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC',
                        str(ROOT/'firmware/targets/c6-lcd-1.47/main/c6_file.c'),'-o',str(lib)],check=True)
        cls.library=ctypes.CDLL(str(lib))
        cls.read=cls.library.c6_file_read
        cls.read.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,
                           ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t]
        cls.read.restype=ctypes.c_bool
        cls.libc=ctypes.CDLL(None)
        cls.libc.fopen.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
        cls.libc.fopen.restype=ctypes.c_void_p
        cls.libc.fclose.argtypes=[ctypes.c_void_p]

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def test_offsets_are_relative_to_the_selected_clip(self):
        path=Path(self.temp.name)/'pack.bin';path.write_bytes(b'PREFIXabcdefSUFFIX')
        f=self.libc.fopen(str(path).encode(),'rb'.encode())
        try:
            out=ctypes.create_string_buffer(3)
            self.assertTrue(self.read(f,6,6,2,out,3))
            self.assertEqual(out.raw,b'cde')
        finally:self.libc.fclose(f)

    def test_rejects_clip_overrun_and_truncated_file(self):
        path=Path(self.temp.name)/'short.bin';path.write_bytes(b'PREFIXabc')
        f=self.libc.fopen(str(path).encode(),b'rb')
        try:
            out=ctypes.create_string_buffer(b'guard')
            self.assertFalse(self.read(f,6,3,2,out,2))
            self.assertEqual(out.value,b'guard')
            self.assertFalse(self.read(f,6,100,2,out,4))
            self.assertFalse(self.read(f,6,3,2**63,out,1))
        finally:self.libc.fclose(f)


if __name__=='__main__':unittest.main()
