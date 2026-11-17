"""Exercise production animation and DMA strip drawing with host LCD boundaries.

Export a decoded packed RGB565 frame through the same C harness with:
  python tests/test_c6_animation_drawing.py --export INPUT.rgb565 OUTPUT_PREFIX
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'firmware/targets/c6-lcd-1.47/main/c6_board.c'


def function(source, name):
    start = re.search(r'^(?:static )?[^\n;]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M).start()
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def compile_harness(directory):
    source = SOURCE.read_text()
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_TIMEOUT 3
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
static struct {int rotation;} state;
static bool fault;
static uint8_t storage[320*16*2], *scanlines=storage;
static uint8_t screen[320*320*2], input[172*280*2], original[172*280*2];
static int transfer_done, panel, calls, fail_at, timeout_at;
static int xSemaphoreTake(int semaphore,int ticks) {
    (void)semaphore;
    return ticks && calls!=timeout_at;
}
static esp_err_t esp_lcd_panel_draw_bitmap(int handle,int x,int y,int right,int bottom,const void *data);
'''
    for name in ('c6_board_width', 'c6_board_height', 'c6_board_draw_rect', 'c6_board_draw_animation'):
        code += function(source, name) + '\n'
    code += r'''
static esp_err_t esp_lcd_panel_draw_bitmap(int handle,int x,int y,int right,int bottom,const void *data) {
    (void)handle;
    assert(data==scanlines);
    assert(x>=0 && y>=0 && right<=c6_board_width() && bottom<=c6_board_height());
    assert(right>x && bottom>y && bottom-y<=16);
    assert((right-x)*(bottom-y)*2<=(int)sizeof(storage));
    ++calls;
    if(calls==fail_at)return 71;
    for(int row=y;row<bottom;++row)
        memcpy(screen+(row*c6_board_width()+x)*2,
               (const uint8_t*)data+(row-y)*(right-x)*2,(right-x)*2);
    return ESP_OK;
}
int main(int argc,char **argv) {
    assert(argc==5);
    state.rotation=atoi(argv[1]);
    FILE *file=fopen(argv[2],"rb");assert(file);
    assert(fread(input,1,sizeof(input),file)==sizeof(input));assert(fgetc(file)==EOF);fclose(file);
    memcpy(original,input,sizeof(input));
    memset(screen,0xa5,sizeof(screen));
    const char *mode=argv[4];
    if(!strcmp(mode,"null")) {
        assert(c6_board_draw_animation(NULL)==ESP_ERR_INVALID_ARG && calls==0);
    } else if(!strcmp(mode,"fault")) {
        fault=true;
        assert(c6_board_draw_animation(input)==ESP_ERR_INVALID_STATE && calls==0);
        assert(c6_board_draw_animation(NULL)==ESP_ERR_INVALID_STATE && calls==0);
    } else if(!strcmp(mode,"error") || !strcmp(mode,"timeout")) {
        if(!strcmp(mode,"error"))fail_at=3;else timeout_at=3;
        assert(c6_board_draw_animation(input)==(fail_at?71:ESP_ERR_TIMEOUT));
        assert(calls==3 && fault);
        assert(c6_board_draw_animation(input)==ESP_ERR_INVALID_STATE && calls==3);
    } else {
        assert(!strcmp(mode,"draw"));
        assert(c6_board_draw_animation(input)==ESP_OK && calls>0 && !fault);
        file=fopen(argv[3],"wb");assert(file);
        size_t size=c6_board_width()*c6_board_height()*2;
        assert(fwrite(screen,1,size,file)==size);fclose(file);
    }
    assert(!memcmp(input,original,sizeof(input)));
    return 0;
}
'''
    path = Path(directory)
    driver = path / 'animation.c'
    driver.write_text(code)
    executable = path / 'animation'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(driver), '-o', str(executable)], check=True)
    return executable


def packed_fixture():
    # Construct natural upright artwork first, then forward-pack clockwise.
    upright = [[bytes(((u * 3 + v * 7) % 255 + 1, (u * 11 + v * 13) % 255 + 1))
                for u in range(280)] for v in range(172)]
    for x, y, width, height, color in (
        (0, 0, 12, 12, b'\xf8\x00'), (268, 0, 12, 12, b'\x07\xe0'),
        (0, 160, 12, 12, b'\x00\x1f'), (268, 160, 12, 12, b'\xff\xe0'),
        (55, 35, 30, 20, b'\x12\x34'), (180, 35, 30, 20, b'\x56\x78'),
        (110, 115, 70, 15, b'\x9a\xbc')):
        for row in range(y, y + height):
            upright[row][x:x + width] = [color] * width
    packed = bytearray(172 * 280 * 2)
    for v, row in enumerate(upright):
        for u, pixel in enumerate(row):
            offset = (u * 172 + 171 - v) * 2
            packed[offset:offset + 2] = pixel
    return upright, packed


class AnimationDrawingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.temp.name)
        cls.executable = compile_harness(cls.path)
        cls.upright, packed = packed_fixture()
        cls.input = cls.path / 'fixture.rgb565'
        cls.input.write_bytes(packed)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_mode(self, rotation, mode):
        output = self.path / 'screen.rgb565'
        subprocess.run([str(self.executable), str(rotation), str(self.input), str(output), mode], check=True)
        return output.read_bytes() if mode == 'draw' else None

    def test_upright_art_exact_fit_padding_and_samples_in_all_rotations(self):
        for rotation in (0, 90, 180, 270):
            with self.subTest(rotation=rotation):
                frame = self.run_mode(rotation, 'draw')
                width, height = (320, 172) if rotation in (90, 270) else (172, 320)
                fit_width, fit_height = (280, 172) if width == 320 else (172, 106)
                left, top = (width - fit_width) // 2, (height - fit_height) // 2
                self.assertEqual(len(frame), width * height * 2)
                # Two horizontal eyes, with mouth below: a sideways image fails
                # on content orientation before the exhaustive margin checks.
                for u, v, color in ((65, 42, b'\x12\x34'), (190, 42, b'\x56\x78'),
                                    (130, 120, b'\x9a\xbc')):
                    x = left + u * fit_width // 280
                    y = top + v * fit_height // 172
                    offset = (y * width + x) * 2
                    self.assertEqual(frame[offset:offset+2], color,
                                     (rotation, u, v, 'upright eye/mouth arrangement'))
                # Forward fixture orientation is independent of renderer inverse sampling.
                for y in range(height):
                    for x in range(width):
                        expected = b'\0\0'
                        if left <= x < left + fit_width and top <= y < top + fit_height:
                            expected = self.upright[(y-top)*172//fit_height][(x-left)*280//fit_width]
                        offset = (y * width + x) * 2
                        self.assertEqual(frame[offset:offset+2], expected,
                                         (rotation, x, y, 'upright pixel / black margin'))

    def test_null_fault_and_immediate_dma_error_propagation(self):
        for rotation in (0, 90, 180, 270):
            for mode in ('null', 'fault', 'error', 'timeout'):
                with self.subTest(rotation=rotation, mode=mode):
                    self.run_mode(rotation, mode)


def export_ppms(input_path, prefix):
    with tempfile.TemporaryDirectory() as temporary:
        executable = compile_harness(temporary)
        for rotation, width, height in ((0, 172, 320), (90, 320, 172)):
            raw = Path(temporary) / 'screen.rgb565'
            subprocess.run([str(executable), str(rotation), str(Path(input_path).resolve()), str(raw), 'draw'], check=True)
            data = raw.read_bytes()
            rgb = bytearray()
            for offset in range(0, len(data), 2):
                value = int.from_bytes(data[offset:offset+2], 'big')
                rgb.extend((((value >> 11) & 31)*255//31, ((value >> 5) & 63)*255//63, (value & 31)*255//31))
            output = Path(f'{prefix}-{rotation}.ppm')
            output.write_bytes(f'P6\n{width} {height}\n255\n'.encode() + rgb)
            print(output.resolve())


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == '--export':
        export_ppms(sys.argv[2], sys.argv[3])
    else:
        unittest.main()
