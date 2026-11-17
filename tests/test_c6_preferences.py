"""Exercise the actual portable C model and NVS adapter without hardware."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CODE = ROOT / "firmware/targets/c6-lcd-1.47/main"
SHARED = ROOT / "firmware/main"


class Preferences(ctypes.Structure):
    _fields_ = [("brightness_percent", ctypes.c_uint8),
                ("orientation", ctypes.c_uint16),
                ("eye_motion", ctypes.c_uint8),
                ("preferred_transport", ctypes.c_uint8)]


def values(prefs):
    return tuple(getattr(prefs, field) for field, _ in prefs._fields_)


ESP_ERR_H = """
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_VERSION 0x10a
#define ESP_ERR_NVS_NOT_FOUND 0x1102
"""
NVS_H = """
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
"""
# NVS is the hardware boundary. Preserve staged/committed semantics and allow
# independent errors at open/read/write/commit; the production adapter is real.
NVS_FAKE = """
#include <assert.h>
#include <string.h>
#include "nvs.h"
static unsigned char stored[128], staged[128];
static size_t stored_size, staged_size;
static int error_stage, writes, closes;
static nvs_open_mode_t open_mode;
void fake_reset(void) { stored_size=staged_size=0; error_stage=writes=closes=0; }
void fake_error(int stage) { error_stage=stage; }
void fake_seed(const void *data, size_t size) {
    assert(size<=sizeof(stored)); memcpy(stored,data,size); stored_size=size;
}
size_t fake_read(void *data) { memcpy(data,stored,stored_size); return stored_size; }
int fake_writes(void) { return writes; }
int fake_closes(void) { return closes; }
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle) {
    assert(strcmp(name,"taby_c6")==0); open_mode=mode;
    if(error_stage==1) return ESP_FAIL;
    *handle=42; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *data,size_t *size) {
    assert(handle==42 && strcmp(key,"preferences")==0 && open_mode==NVS_READONLY);
    if(error_stage==2 || (data && error_stage==5)) return ESP_FAIL;
    if(!stored_size) return ESP_ERR_NVS_NOT_FOUND;
    if(data) { if(*size<stored_size) return ESP_ERR_INVALID_SIZE; memcpy(data,stored,stored_size); }
    *size=stored_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t size) {
    assert(handle==42 && strcmp(key,"preferences")==0 && open_mode==NVS_READWRITE);
    ++writes;
    if(error_stage==3) return ESP_FAIL;
    assert(size<=sizeof(staged)); memcpy(staged,data,size); staged_size=size; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle==42);
    if(error_stage==4) return ESP_FAIL;
    memcpy(stored,staged,staged_size); stored_size=staged_size; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle==42); ++closes; }
"""


class C6PreferencesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        (folder / "esp_err.h").write_text(ESP_ERR_H)
        (folder / "nvs.h").write_text(NVS_H)
        (folder / "nvs_fake.c").write_text(NVS_FAKE)
        lib = folder / "preferences.dylib"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-shared", "-fPIC", "-DESP_PLATFORM", f"-I{folder}",
                        f"-I{CODE}", f"-I{SHARED}",
                        str(CODE / "c6_preferences.c"),
                        str(CODE / "c6_preferences_nvs.c"),
                        str(SHARED / "taby_eye_motion.c"),
                        str(folder / "nvs_fake.c"), "-o", str(lib)], check=True)
        cls.lib = ctypes.CDLL(str(lib))
        p = ctypes.POINTER(Preferences)
        cls.lib.c6_preferences_defaults.argtypes = [p]
        cls.lib.c6_preferences_valid.argtypes = [p]
        cls.lib.c6_preferences_valid.restype = ctypes.c_bool
        cls.lib.c6_preferences_command.argtypes = [p, ctypes.c_char_p, p,
                                                  ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.c6_preferences_command.restype = ctypes.c_int
        cls.lib.c6_preferences_load.argtypes = [p]
        cls.lib.c6_preferences_save.argtypes = [p]
        cls.lib.fake_seed.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.fake_read.argtypes = [ctypes.c_void_p]
        cls.lib.fake_read.restype = ctypes.c_size_t

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.fake_reset()
        self.current = Preferences(70, 0, 0, 0)

    def command(self, text, size=256, alias=False):
        output = self.current if alias else Preferences(1, 90, 2, 2)
        reply = ctypes.create_string_buffer(size)
        result = self.lib.c6_preferences_command(ctypes.byref(self.current), text,
                                                  ctypes.byref(output), reply, size)
        return result, values(output), reply.value.decode()

    def blob(self):
        output = ctypes.create_string_buffer(128)
        size = self.lib.fake_read(output)
        return output.raw[:size]

    def test_defaults_are_valid_and_queryable(self):
        self.current = Preferences(255, 65535, 255, 255)
        self.lib.c6_preferences_defaults(ctypes.byref(self.current))
        self.assertEqual(values(self.current), (70, 0, 0, 0))
        self.assertTrue(self.lib.c6_preferences_valid(ctypes.byref(self.current)))

    def test_validates_every_stored_mode_and_rejects_out_of_range(self):
        for orientation in (0, 90, 180, 270, 360):
            for eye in (0, 1, 2):
                for transport in (0, 1, 2):
                    p = Preferences(100, orientation, eye, transport)
                    self.assertTrue(self.lib.c6_preferences_valid(ctypes.byref(p)))
        for state in ((101, 0, 0, 0), (255, 0, 0, 0), (70, 1, 0, 0),
                      (70, 65535, 0, 0), (70, 0, 3, 0), (70, 0, 0, 3)):
            self.assertFalse(self.lib.c6_preferences_valid(ctypes.byref(Preferences(*state))))
        self.assertFalse(self.lib.c6_preferences_valid(None))

    def test_queries_match_upstream_reply_shapes_without_mutation(self):
        for text, expected in (
            (b"BRIGHTNESS", 'TABY:BRIGHTNESS {"percent":70,"raw":179}'),
            (b"BRIGHTNESS?", 'TABY:BRIGHTNESS {"percent":70,"raw":179}'),
            (b"DISPLAY_ORIENTATION", 'TABY:DISPLAY_ORIENTATION {"mode":"left","orientation":"left","rotation_degrees":0,"auto_supported":true,"display_shape":"rectangle"}'),
            (b"DISPLAY_ORIENTATION?", 'TABY:DISPLAY_ORIENTATION {"mode":"left","orientation":"left","rotation_degrees":0,"auto_supported":true,"display_shape":"rectangle"}'),
            (b"EYE_MOTION", 'TABY:EYE_MOTION {"mode":"normal","modes":["normal","calm","still"]}'),
            (b"EYE_MOTION?", 'TABY:EYE_MOTION {"mode":"normal","modes":["normal","calm","still"]}'),
        ):
            with self.subTest(text=text):
                self.assertEqual(self.command(text), (1, (70, 0, 0, 0), expected))

    def test_brightness_mutation_boundaries_and_raw_rounding(self):
        for text, percent, raw in ((b"BRIGHTNESS 0", 0, 0), (b"BRIGHTNESS 100", 100, 255),
                                   (b"BRIGHTNESS   1 \t", 1, 3)):
            self.assertEqual(self.command(text),
                             (2, (percent, 0, 0, 0), f'TABY:BRIGHTNESS {{"percent":{percent},"raw":{raw}}}'))

    def test_orientation_names_numbers_and_auto_are_validated(self):
        for token, degree, name in (("left", 0, "left"), ("0", 0, "left"),
                                    ("RIGHT", 180, "right"), ("180", 180, "right"),
                                    ("90", 90, "90"), ("270", 270, "270"),
                                    ("auto", 360, "auto")):
            with self.subTest(token=token):
                result, state, reply = self.command(f"DISPLAY_ORIENTATION {token}".encode())
                self.assertEqual((result, state), (2, (70, degree, 0, 0)))
                effective, rotation = ("left", 0) if degree == 360 else (name, degree)
                self.assertEqual(reply, f'TABY:DISPLAY_ORIENTATION {{"mode":"{name}","orientation":"{effective}","rotation_degrees":{rotation},"auto_supported":true,"display_shape":"rectangle"}}')

    def test_eye_motion_uses_upstream_grammar(self):
        for token, eye in (("normal", 0), ("calm", 1), ("still", 2)):
            self.assertEqual(self.command(f"EYE_MOTION\t{token} \r\n".encode()),
                             (2, (70, 0, eye, 0), f'TABY:EYE_MOTION {{"mode":"{token}","modes":["normal","calm","still"]}}'))

    def test_transport_default_accepts_upstream_names(self):
        for token, value in (("usb", 0), ("wifi", 1), ("ble", 2), ("bluetooth", 2)):
            self.assertEqual(self.command(f"TRANSPORT_DEFAULT {token}".encode()),
                             (2, (70, 0, 0, value), "TABY:OK TRANSPORT_DEFAULT"))

    def test_invalid_commands_return_specific_errors_without_mutation(self):
        for prefix, tokens, error in (
            ("BRIGHTNESS", ("", "-1", "101", "12junk", "12 3", "999999999999999999999999999999999999", "no"), "brightness_invalid_value"),
            ("DISPLAY_ORIENTATION", ("", "45", "360", "left junk", "auto junk"), "display_orientation_invalid_value"),
            ("EYE_MOTION", ("", "CALM", "3", "calm junk"), "eye_motion_invalid_value"),
            ("TRANSPORT_DEFAULT", ("", "3", "USB", "usb junk"), "invalid_transport_mode"),
        ):
            for token in tokens:
                with self.subTest(prefix=prefix, token=token):
                    self.assertEqual(self.command(f"{prefix} {token}".encode()),
                                     (3, (70, 0, 0, 0), f"TABY:ERR {error}"))

    def test_unhandled_commands_do_not_claim_transport_activation(self):
        for text in (b"PING", b"TRANSPORT_MODE wifi", b"EYE_MOTION_EXTRA", b"BRIGHTNESS_EXTRA", b""):
            self.assertEqual(self.command(text), (0, (70, 0, 0, 0), ""))

    def test_tiny_reply_buffers_reject_mutation_and_terminate(self):
        for size in (0, 1, 8):
            self.assertEqual(self.command(b"BRIGHTNESS 80", size=size), (3, (70, 0, 0, 0), ""))

    def test_aliasing_output_and_current_is_safe(self):
        self.assertEqual(self.command(b"EYE_MOTION calm", alias=True)[0:2], (2, (70, 0, 1, 0)))
        self.assertEqual(self.command(b"EYE_MOTION junk", alias=True)[0:2], (3, (70, 0, 1, 0)))

    def test_missing_storage_defaults_and_reports_not_found(self):
        output = Preferences(5, 90, 2, 2)
        self.assertEqual(self.lib.c6_preferences_load(ctypes.byref(output)), 0x1102)
        self.assertEqual(values(output), (70, 0, 0, 0))
        self.assertEqual(self.lib.fake_writes(), 0)

    def test_save_round_trip_has_versioned_stable_bytes(self):
        source = Preferences(34, 270, 2, 1)
        self.assertEqual(self.lib.c6_preferences_save(ctypes.byref(source)), 0)
        self.assertEqual(self.blob(), b"TC6P\x01\x00\x10\x00\x22\x0e\x01\x02\x01\x00\x00\x00")
        output = Preferences()
        self.assertEqual(self.lib.c6_preferences_load(ctypes.byref(output)), 0)
        self.assertEqual(values(output), (34, 270, 2, 1))

    def test_invalid_stored_schema_length_version_and_values_default_without_erasing(self):
        good = b"TC6P\x01\x00\x10\x00\x46\x00\x00\x00\x00\x00\x00\x00"
        bad_blobs = [b"bad", good + b"extra", b"XXXX" + good[4:],
                     good[:4] + b"\x02\x00" + good[6:], good[:6] + b"\x0f" + good[7:]]
        for offset, value in ((8, 101), (9, 45), (11, 3), (12, 3)):
            bad = bytearray(good)
            bad[offset] = value
            bad_blobs.append(bytes(bad))
        for blob in bad_blobs:
            with self.subTest(blob=blob):
                self.lib.fake_seed(blob, len(blob))
                output = Preferences(1, 90, 2, 2)
                self.assertNotEqual(self.lib.c6_preferences_load(ctypes.byref(output)), 0)
                self.assertEqual(values(output), (70, 0, 0, 0))
                self.assertEqual(self.blob(), blob)
                self.assertEqual(self.lib.fake_writes(), 0)

    def test_storage_errors_propagate_and_leave_committed_data_intact(self):
        self.assertEqual(self.lib.c6_preferences_save(ctypes.byref(self.current)), 0)
        previous = self.blob()
        changed = Preferences(10, 180, 1, 2)
        for stage in (1, 3, 4):
            self.lib.fake_error(stage)
            self.assertEqual(self.lib.c6_preferences_save(ctypes.byref(changed)), -1)
            self.assertEqual(self.blob(), previous)
        for stage in (1, 2, 5):
            self.lib.fake_error(stage)
            output = Preferences(1, 90, 1, 1)
            self.assertEqual(self.lib.c6_preferences_load(ctypes.byref(output)), -1)
            self.assertEqual(values(output), (70, 0, 0, 0))
            self.assertEqual(self.blob(), previous)

    def test_invalid_save_does_not_open_or_write_and_nulls_are_safe(self):
        invalid = Preferences(101, 0, 0, 0)
        self.assertEqual(self.lib.c6_preferences_save(ctypes.byref(invalid)), 0x102)
        self.assertEqual(self.lib.fake_writes(), 0)
        self.assertEqual(self.lib.fake_closes(), 0)
        self.assertEqual(self.lib.c6_preferences_save(None), 0x102)
        self.assertEqual(self.lib.c6_preferences_load(None), 0x102)
        self.lib.c6_preferences_defaults(None)

    def test_invalid_parser_arguments_never_mutate_current(self):
        reply = ctypes.create_string_buffer(256)
        output = Preferences()
        for current, command, next_state, buffer, size in (
            (None, b"BRIGHTNESS 1", ctypes.byref(output), reply, 256),
            (ctypes.byref(self.current), None, ctypes.byref(output), reply, 256),
            (ctypes.byref(self.current), b"BRIGHTNESS 1", None, reply, 256),
            (ctypes.byref(self.current), b"BRIGHTNESS 1", ctypes.byref(output), None, 256),
        ):
            self.assertEqual(self.lib.c6_preferences_command(current, command, next_state, buffer, size), 3)
            self.assertEqual(values(self.current), (70, 0, 0, 0))
        self.current.orientation = 45
        self.assertEqual(self.command(b"BRIGHTNESS 1"), (3, (70, 45, 0, 0), ""))

    def test_portable_header_and_model_compile_without_esp_idf(self):
        folder = Path(self.temp.name)
        driver = folder / "portable_driver.c"
        driver.write_text('''
#include <assert.h>
#include "c6_preferences.h"
int main(void) {
    c6_preferences_t p;
    c6_preferences_defaults(&p);
    assert(c6_preferences_valid(&p));
    char reply[256];
    assert(c6_preferences_command(&p, "BRIGHTNESS 80", &p, reply, sizeof(reply)) == C6_PREF_CHANGED);
    assert(p.brightness_percent == 80);
    return 0;
}
''')
        executable = folder / "portable_driver"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        f"-I{CODE}", f"-I{SHARED}", str(driver),
                        str(CODE / "c6_preferences.c"), str(SHARED / "taby_eye_motion.c"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
