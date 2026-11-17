"""Catch touch/image disagreement after rotation and unstable flat-board auto rotation."""
import ctypes
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]


class GeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();p=Path(cls.temp.name)/'geometry.dylib'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC',str(ROOT/'firmware/targets/c6-lcd-1.47/main/c6_geometry.c'),'-o',str(p)],check=True)
        cls.lib=ctypes.CDLL(str(p));cls.lib.c6_touch_rotate.restype=ctypes.c_bool
        cls.lib.c6_gravity_rotation.restype=ctypes.c_uint16
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def test_touch_corners_match_vendor_rotation_flags(self):
        for r,want in [(0,(171,0)),(90,(0,0)),(180,(0,319)),(270,(319,171))]:
            x=ctypes.c_uint16();y=ctypes.c_uint16()
            self.assertTrue(self.lib.c6_touch_rotate(0,0,r,ctypes.byref(x),ctypes.byref(y)))
            self.assertEqual((x.value,y.value),want)
        x=ctypes.c_uint16();y=ctypes.c_uint16()
        self.assertFalse(self.lib.c6_touch_rotate(172,0,0,ctypes.byref(x),ctypes.byref(y)))
    def test_gravity_selects_cardinal_pose_but_holds_when_flat_or_ambiguous(self):
        f=self.lib.c6_gravity_rotation
        for args,want in [((0,-1000,0,180),0),((0,1000,0,0),180),((1000,0,0,0),90),((-1000,0,0,0),270),((0,0,1000,90),90),((700,700,0,180),180),((200,200,0,270),270)]:
            self.assertEqual(f(*args),want)

if __name__=='__main__':unittest.main()
