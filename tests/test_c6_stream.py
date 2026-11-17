"""Compile the real streaming decoder and exercise it through the FILE adapter."""
import ctypes
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'firmware/targets/c6-lcd-1.47/main'
SIZE_MAX = ctypes.c_size_t(-1).value
PALETTE = bytes(range(128))

# The adapter deliberately uses c6_file_read, so truncation and clip bounds are
# checked by production file I/O as well as the streaming decoder's contract.
ADAPTER = r'''
#include "c6_stream.h"
#include "c6_file.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t calls, largest_read, bytes_read, invalid_read, scratch_guard;
} stats_t;
typedef struct {
    FILE *file;
    uint32_t base, clip_length;
    size_t encoded_offset, encoded_length, fail_at;
    stats_t *stats;
} source_t;
static bool read_source(void *context, size_t offset, uint8_t *out, size_t size) {
    source_t *source = context;
    source->stats->calls++;
    if (size > source->stats->largest_read) source->stats->largest_read = size;
    if (!size || size > 4096 || offset > source->encoded_length ||
        size > source->encoded_length-offset ||
        offset > SIZE_MAX-source->encoded_offset) {
        source->stats->invalid_read++;
        return false;
    }
    if (offset >= source->fail_at) return false;
    bool ok = c6_file_read(source->file, source->base, source->clip_length,
                          source->encoded_offset+offset, out, size);
    if (ok) source->stats->bytes_read += size;
    return ok;
}
bool test_decode_file(const char *path, uint32_t base, uint32_t clip_length,
                      size_t encoded_offset, size_t encoded_length,
                      const uint8_t *palette, uint8_t *out, size_t pixels,
                      size_t scratch_size, size_t fail_at, stats_t *stats) {
    memset(stats, 0, sizeof(*stats));
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t *guarded = malloc(scratch_size+2);
    if (!guarded) { fclose(file); return false; }
    memset(guarded, 0xa5, scratch_size+2);
    source_t source = {file, base, clip_length, encoded_offset, encoded_length,
                       fail_at, stats};
    bool ok = c6_stream_decode(read_source, &source, encoded_length, palette,
                              out, pixels, guarded+1, scratch_size);
    stats->scratch_guard = guarded[0] == 0xa5 && guarded[scratch_size+1] == 0xa5;
    free(guarded);
    fclose(file);
    return ok;
}
'''


SD_STUBS = {
    'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n',
    'driver/sdspi_host.h': r'''#pragma once
#define SPI2_HOST 2
typedef struct { int slot, max_freq_khz; } sdmmc_host_t;
typedef struct { int host_id, gpio_cs; } sdspi_device_config_t;
#define SDSPI_HOST_DEFAULT() ((sdmmc_host_t){0})
#define SDSPI_DEVICE_CONFIG_DEFAULT() ((sdspi_device_config_t){0})
''',
    'esp_crc.h': r'''#pragma once
#include <stdint.h>
#include <stddef.h>
static inline uint32_t esp_crc32_le(uint32_t crc, const uint8_t *data, size_t n) {
    (void)crc; (void)data; (void)n; return 0;
}
''',
    'sdmmc_cmd.h': '#pragma once\ntypedef struct { int unused; } sdmmc_card_t;\n',
    'esp_vfs_fat.h': r'''#pragma once
#include <stdbool.h>
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
typedef struct { bool format_if_mount_failed; int max_files, allocation_unit_size; }
    esp_vfs_fat_sdmmc_mount_config_t;
static inline int esp_vfs_fat_sdspi_mount(const char *path, sdmmc_host_t *host,
        sdspi_device_config_t *slot, esp_vfs_fat_sdmmc_mount_config_t *config,
        sdmmc_card_t **card) {
    (void)path; (void)host; (void)slot; (void)config; (void)card; return -1;
}
''',
    'freertos/FreeRTOS.h': '#pragma once\n#define pdMS_TO_TICKS(ms) (ms)\n',
    'freertos/task.h': '#pragma once\nstatic inline void vTaskDelay(int n) { (void)n; }\n',
}
SD_ADAPTER = r'''
/* Only the platform mount/CRC calls are stubbed. Tests select the actual pack
 * FILE and catalog entry inside this translation unit without production hooks. */
#include "c6_sd.c"
bool test_sd_open(const char *path) {
    fail("test_reset");
    pack = fopen(path, "rb");
    buffer = malloc(4096);
    ready = pack && buffer;
    return ready;
}
void test_sd_close(void) { fail("test_done"); }
bool test_sd_decode(size_t clip_index, size_t offset, size_t length,
                    uint8_t *out, size_t pixels) {
    return c6_sd_decode(c6_sd_at(clip_index), offset, length, out, pixels);
}
bool test_sd_read(size_t clip_index, size_t offset, size_t length) {
    return c6_sd_read(c6_sd_at(clip_index), offset, length) != NULL;
}
bool test_sd_decode_foreign(uint8_t *out) {
    c6_clip_t foreign = {"fake", 0, 1000};
    return c6_sd_decode(&foreign, 12, 130, out, 1);
}
'''


class Stats(ctypes.Structure):
    _fields_ = [(name, ctypes.c_size_t) for name in
                ('calls', 'largest_read', 'bytes_read', 'invalid_read', 'scratch_guard')]


class C6StreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.library = None

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        if self.__class__.library is None:
            adapter = Path(self.temp.name) / 'adapter.c'
            adapter.write_text(ADAPTER)
            for name, content in SD_STUBS.items():
                stub = Path(self.temp.name) / name
                stub.parent.mkdir(parents=True, exist_ok=True)
                stub.write_text(content)
            sd_adapter = Path(self.temp.name) / 'sd_adapter.c'
            sd_adapter.write_text(SD_ADAPTER)
            lib = Path(self.temp.name) / 'stream.dylib'
            result = subprocess.run(
                ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                 '-Wframe-larger-than=1024', '-I', str(MAIN), '-I', self.temp.name,
                 str(adapter), str(sd_adapter),
                 str(MAIN/'c6_stream.c'),
                 str(MAIN/'c6_file.c'), str(MAIN/'c6_codec.c'), '-o', str(lib)],
                text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            library = ctypes.CDLL(str(lib))
            library.test_decode_file.argtypes = [
                ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32,
                ctypes.c_size_t, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p,
                ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t, ctypes.POINTER(Stats)]
            library.test_decode_file.restype = ctypes.c_bool
            library.c6_rle_decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                              ctypes.c_void_p, ctypes.c_void_p,
                                              ctypes.c_size_t]
            library.c6_rle_decode.restype = ctypes.c_bool
            library.test_sd_open.argtypes = [ctypes.c_char_p]
            library.test_sd_open.restype = ctypes.c_bool
            library.test_sd_close.argtypes = []
            library.test_sd_decode.argtypes = [ctypes.c_size_t, ctypes.c_size_t,
                                                ctypes.c_size_t, ctypes.c_void_p,
                                                ctypes.c_size_t]
            library.test_sd_decode.restype = ctypes.c_bool
            library.test_sd_read.argtypes = [ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t]
            library.test_sd_read.restype = ctypes.c_bool
            library.test_sd_decode_foreign.argtypes = [ctypes.c_void_p]
            library.test_sd_decode_foreign.restype = ctypes.c_bool
            self.__class__.library = library

    def decode(self, data, pixels, chunk=4096, *, length=None, clip_length=None,
               fail_at=SIZE_MAX, palette=PALETTE, null_out=False):
        path = Path(self.temp.name) / 'fixture.bin'
        path.write_bytes(b'PREFIX' + data)
        return self.decode_file(path, 6, len(data) if clip_length is None else clip_length,
                                0, len(data) if length is None else length,
                                pixels, chunk, fail_at, palette, null_out)

    def decode_file(self, path, base, clip_length, offset, length, pixels, chunk,
                    fail_at=SIZE_MAX, palette=PALETTE, null_out=False):
        guarded = ctypes.create_string_buffer(b'\xa5' * (2*pixels+2), 2*pixels+2)
        stats = Stats()
        out = None if null_out else ctypes.byref(guarded, 1)
        ok = self.library.test_decode_file(
            str(path).encode(), base, clip_length, offset, length, palette, out,
            pixels, chunk, fail_at, ctypes.byref(stats))
        self.assertEqual(guarded.raw[0], 0xa5)
        self.assertEqual(guarded.raw[-1], 0xa5)
        self.assertEqual(stats.scratch_guard, 1)
        self.assertEqual(stats.invalid_read, 0)
        self.assertLessEqual(stats.largest_read, min(chunk, 4096))
        return ok, guarded.raw[1:-1], stats

    def test_mixed_tokens_keep_rgb565_byte_order(self):
        ok, out, stats = self.decode(b'\x02\x00\x3f\x01\x82\x02', 6, 2)
        self.assertTrue(ok)
        self.assertEqual(out, b'\x00\x01\x7e\x7f\x02\x03\x04\x05\x04\x05\x04\x05')
        self.assertEqual(stats.bytes_read, 6)

    def test_every_token_count_and_literal_split_boundary(self):
        tokens, expected = bytearray(), bytearray()
        for count in range(1, 129):
            indices = bytes(index % 64 for index in range(count))
            tokens.extend(bytes([count-1]) + indices)
            expected.extend(b''.join(PALETTE[2*index:2*index+2] for index in indices))
            tokens.extend(bytes([127+count, 63]))
            expected.extend(b'\x7e\x7f' * count)
        # Sizes 1..129 force literal bytes and repeat indices across every
        # position of a maximal token. Larger reads exercise the SD buffer size.
        for chunk in [*range(1, 130), 1024, 4096]:
            with self.subTest(chunk=chunk):
                ok, out, stats = self.decode(tokens, len(expected)//2, chunk)
                self.assertTrue(ok)
                self.assertEqual(out, expected)
                self.assertEqual(stats.bytes_read, len(tokens))

    def test_maximum_literal_split_at_every_byte(self):
        data = b'\x7f' + bytes(range(64))*2
        for chunk in range(1, 130):
            with self.subTest(chunk=chunk):
                ok, out, _ = self.decode(data, 128, chunk)
                self.assertTrue(ok)
                self.assertEqual(out, PALETTE*2)

    def test_rejects_truncation_invalid_indices_trailing_data_and_overrun(self):
        cases = [
            (b'', 1), (b'\x80', 1), (b'\x02\x00\x01', 3),
            (b'\x80\x40', 1), (b'\x01\x00\xff', 2),
            (b'\x80\x00\x80\x00', 1), (b'\x81\x00', 1),
            (b'\x01\x00\x01', 1), (b'\x80\x00', 2),
        ]
        for data, pixels in cases:
            for chunk in (1, 2, 4096):
                with self.subTest(data=data, pixels=pixels, chunk=chunk):
                    self.assertFalse(self.decode(data, pixels, chunk)[0])

    def test_file_short_read_clip_overrun_and_callback_failure(self):
        self.assertFalse(self.decode(b'\x81\x00', 2, 1, length=3, clip_length=3)[0])
        self.assertFalse(self.decode(b'\x81\x00', 2, 1, clip_length=1)[0])
        self.assertFalse(self.decode(b'\x81\x00', 2, 1, fail_at=1)[0])

    def test_invalid_arguments_do_not_read_or_write(self):
        for options in ({'pixels': 0}, {'pixels': 1, 'chunk': 0},
                        {'pixels': 1, 'chunk': 4097},
                        {'pixels': 1, 'palette': None},
                        {'pixels': 1, 'null_out': True}):
            with self.subTest(options=options):
                ok, out, stats = self.decode(b'\x80\x00', **options)
                self.assertFalse(ok)
                self.assertEqual(stats.calls, 0)
                self.assertEqual(out, b'\xa5' * len(out))

    def test_sd_decode_validates_payload_and_read_size(self):
        path = Path(self.temp.name) / 'sd_fixture.bin'
        path.write_bytes(b'X'*12 + PALETTE + b'\x80\x3f')
        self.assertTrue(self.library.test_sd_open(str(path).encode()))
        guarded = ctypes.create_string_buffer(b'\xa5'*4, 4)
        out = ctypes.byref(guarded, 1)
        try:
            self.assertTrue(self.library.test_sd_decode(0, 12, 130, out, 1))
            self.assertEqual(guarded.raw, b'\xa5\x7e\x7f\xa5')
            self.assertTrue(self.library.test_sd_read(0, 0, 12))
            self.assertFalse(self.library.test_sd_read(0, 0, 4097))
            for offset, length in [(12, 127), (12, 128), (12, 131),
                                   (163758, 130), (SIZE_MAX, 130),
                                   (12, SIZE_MAX), (12, 48666)]:
                with self.subTest(offset=offset, length=length):
                    self.assertTrue(self.library.test_sd_open(str(path).encode()))
                    self.assertFalse(self.library.test_sd_decode(0, offset, length, out, 1))
            self.assertTrue(self.library.test_sd_open(str(path).encode()))
            self.assertFalse(self.library.test_sd_decode_foreign(out))
        finally:
            self.library.test_sd_close()
        self.assertFalse(self.library.test_sd_decode(0, 12, 130, out, 1))

    def test_sd_accepts_maximum_encoded_payload_with_small_reads(self):
        pixels = 172*280
        full_tokens, remainder = divmod(pixels, 128)
        encoded = (b'\x7f' + b'\x00'*128)*full_tokens
        if remainder:
            encoded += bytes([remainder-1]) + b'\x00'*remainder
        payload = PALETTE + encoded
        path = Path(self.temp.name) / 'sd_maximum.bin'
        path.write_bytes(b'X'*12 + payload)
        self.assertTrue(self.library.test_sd_open(str(path).encode()))
        self.addCleanup(self.library.test_sd_close)
        guarded = ctypes.create_string_buffer(b'\xa5'*(2*pixels+2), 2*pixels+2)
        self.assertTrue(self.library.test_sd_decode(
            0, 12, len(payload), ctypes.byref(guarded, 1), pixels))
        self.assertEqual(guarded.raw, b'\xa5' + b'\x00\x01'*pixels + b'\xa5')

    def test_sd_rejects_truncated_palette(self):
        path = Path(self.temp.name) / 'sd_palette.bin'
        path.write_bytes(b'X'*12 + PALETTE[:-1])
        self.assertTrue(self.library.test_sd_open(str(path).encode()))
        self.addCleanup(self.library.test_sd_close)
        out = ctypes.create_string_buffer(b'\xa5'*2, 2)
        self.assertFalse(self.library.test_sd_decode(0, 12, 130, out, 1))
        self.assertEqual(out.raw, b'\xa5'*2)
        self.assertFalse(self.library.test_sd_read(0, 0, 12))

    def test_packaged_sd_frames_match_existing_decoder(self):
        folder = ROOT / 'dist/c6-lcd-1.47-sd/taby'
        if not (folder/'assets.bin').exists():
            self.skipTest('optional generated full SD pack is absent')
        catalog = json.loads((folder/'assets.json').read_text())
        pack = (folder/'assets.bin').read_bytes()
        frames = 0
        self.assertTrue(self.library.test_sd_open(str(folder/'assets.bin').encode()))
        self.addCleanup(self.library.test_sd_close)
        for clip_index, clip in enumerate(catalog['clips']):
            base, size = clip['offset'], clip['size']
            magic, width, height, count, _ = struct.unpack_from('<4sHHHH', pack, base)
            self.assertEqual(magic, b'TC6A')
            self.assertEqual(count, clip['frames'])
            cursor = 12
            reference = ctypes.create_string_buffer(width*height*2)
            sd_guarded = ctypes.create_string_buffer(b'\xa5'*(width*height*2+2), width*height*2+2)
            for frame in range(count):
                duration, length = struct.unpack_from('<HI', pack, base+cursor)
                self.assertGreater(duration, 0)
                cursor += 6
                palette = pack[base+cursor:base+cursor+128]
                encoded = pack[base+cursor+128:base+cursor+length]
                with self.subTest(clip=clip['id'], frame=frame):
                    self.assertTrue(self.library.c6_rle_decode(
                        encoded, len(encoded), palette, reference, width*height))
                    ok, out, stats = self.decode_file(
                        folder/'assets.bin', base, size, cursor+128, length-128,
                        width*height, 4096, palette=palette)
                    self.assertTrue(ok)
                    self.assertEqual(out, reference.raw)
                    self.assertEqual(stats.bytes_read, length-128)
                    self.assertTrue(self.library.test_sd_decode(
                        clip_index, cursor, length, ctypes.byref(sd_guarded, 1), width*height))
                    self.assertEqual(sd_guarded.raw, b'\xa5'+reference.raw+b'\xa5')
                frames += 1
                cursor += length
            self.assertEqual(cursor, size)
        self.assertEqual(frames, 3367)  # Upstream rectangle catalog0.4.1.


if __name__ == '__main__':
    unittest.main()
