"""Portable checks run actual C parsing, framing and atomic profile transitions."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
MAIN=ROOT/'firmware/targets/c6-lcd-1.47/main'

class Profile(C.Structure):
    _fields_=[('ssid',C.c_char*33),('password',C.c_char*65),('priority',C.c_uint8),('preferred',C.c_uint8),('last_success',C.c_uint8)]
class Store(C.Structure):
    _fields_=[('version',C.c_uint8),('count',C.c_uint8),('profiles',Profile*5)]

class NetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(); lib=Path(cls.tmp.name)/'network.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC',str(MAIN/'c6_net_helpers.c'),'-o',str(lib)],check=True)
        cls.lib=C.CDLL(str(lib))
        for name in ['c6_net_credentials_valid','c6_net_decode','c6_net_command_bytes','c6_net_frame','c6_net_store_valid','c6_net_upsert','c6_net_forget','c6_net_prefer','c6_net_save']:
            getattr(cls.lib,name).restype=C.c_bool
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_credentials_byte_limits_and_psk(self):
        f=self.lib.c6_net_credentials_valid
        for s,p in [(b'a',b''),(b'x'*32,b'p'*63),(b'a',b'0123456789abcdef'*4),(b'\xc3\xa4'*16,b'12345678')]:self.assertTrue(f(s,p))
        for s,p in [(b'',b''),(b'x'*33,b''),(b'a',b'1234567'),(b'a',b'z'*64),(b'a',b'p'*65),(b'a\n',b'12345678')]:self.assertFalse(f(s,p))
    def test_json_nul_escape_distinguishes_literal_backslash(self):
        f=self.lib.c6_net_json_no_nul;f.restype=C.c_bool
        self.assertFalse(f(br'{"ssid":"one\u0000two"}'))
        self.assertTrue(f(br'{"ssid":"one\\u0000two"}'))
        self.assertTrue(f(br'{"ssid":"one\u0020two"}'))
    def test_json_depth_and_length_are_bounded_before_recursive_parser(self):
        f=self.lib.c6_net_json_safe;f.restype=C.c_bool
        self.assertTrue(f(br'{"ssid":"[{}]\"x"}'))
        self.assertFalse(f(b'['*9+b'0'+b']'*9))
        self.assertFalse(f(b' '*1025))
        self.assertFalse(f(br'{"password":"abc\u0000def"}'))
    def test_uri_decode_rejects_injection_and_truncation(self):
        for encoded,want in [(b'UI%2Ftext+Hello%20world',b'UI/text Hello world'),(b'%2520',b'%20')]:
            out=C.create_string_buffer(128);self.assertTrue(self.lib.c6_net_decode(encoded,len(encoded),out,len(out)));self.assertEqual(out.value,want)
        for bad in [b'%',b'%2',b'%xx',b'%00',b'%0a',b'%7f',b'a\x00b',b'one\rtwo']:
            out=C.create_string_buffer(128);self.assertFalse(self.lib.c6_net_decode(bad,len(bad),out,len(out)));self.assertEqual(out.value,b'')
        out=C.create_string_buffer(3);self.assertFalse(self.lib.c6_net_decode(b'abc',3,out,3))
    def test_ble_input_and_event_framing(self):
        out=C.create_string_buffer(256)
        self.assertTrue(self.lib.c6_net_command_bytes(b'PING\r\n',6,out,256));self.assertEqual(out.value,b'PING')
        for bad in [b'PING\nCLEAR',b'PING\0CLEAR',b'',b'x'*256]:self.assertFalse(self.lib.c6_net_command_bytes(bad,len(bad),out,256))
        self.assertTrue(self.lib.c6_net_frame(b'TABY:OK idle',out,256));self.assertEqual(out.value,b'OK idle')
        small=C.create_string_buffer(8);self.assertFalse(self.lib.c6_net_frame(b'TABY:OK longresponse',small,8));self.assertEqual(small.value,b'')
    def test_profile_capacity_preference_and_corruption(self):
        s=Store();s.version=1
        self.assertTrue(self.lib.c6_net_store_valid(C.byref(s),C.sizeof(s)))
        for i in range(5):self.assertTrue(self.lib.c6_net_upsert(C.byref(s),f'net{i}'.encode(),b'12345678'))
        before=bytes(s);self.assertFalse(self.lib.c6_net_upsert(C.byref(s),b'sixth',b''));self.assertEqual(bytes(s),before)
        self.assertTrue(self.lib.c6_net_prefer(C.byref(s),b'net2'));self.assertEqual(sum(p.preferred for p in s.profiles),1);self.assertEqual(s.profiles[2].priority,5);self.assertEqual(sorted(p.priority for p in s.profiles),[1,2,3,4,5])
        self.assertTrue(self.lib.c6_net_upsert(C.byref(s),b'net2',b'newpass12'));self.assertEqual(s.profiles[2].password,b'newpass12')
        self.assertTrue(self.lib.c6_net_forget(C.byref(s),b'net2'));self.assertEqual(s.count,4)
        self.assertFalse(self.lib.c6_net_store_valid(C.byref(s),C.sizeof(s)-1))
        s.version=2;self.assertFalse(self.lib.c6_net_store_valid(C.byref(s),C.sizeof(s)))
        s.version=1;s.profiles[0].ssid=b'x'*33;self.assertFalse(self.lib.c6_net_store_valid(C.byref(s),C.sizeof(s)))
    def test_failed_storage_commit_preserves_live_profiles(self):
        old=Store();old.version=1;new=Store();new.version=1
        self.lib.c6_net_upsert(C.byref(new),b'network',b'12345678')
        cbtype=C.CFUNCTYPE(C.c_bool,C.c_void_p,C.c_size_t,C.c_void_p)
        called=[]
        @cbtype
        def fail(blob,size,ctx):called.append(size);return False
        self.assertFalse(self.lib.c6_net_save(C.byref(old),C.byref(new),fail,None));self.assertEqual(old.count,0);self.assertEqual(called,[C.sizeof(new)])
        @cbtype
        def good(blob,size,ctx):return True
        self.assertTrue(self.lib.c6_net_save(C.byref(old),C.byref(new),good,None));self.assertEqual(bytes(old),bytes(new))

if __name__=='__main__':unittest.main()
