"""Run the portable UI state machine as real C, without ESP-IDF or LVGL."""
from pathlib import Path
import importlib.util
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CODE = ROOT / "firmware/targets/c6-lcd-1.47/main"
SHARED = ROOT / "firmware/main"

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "c6_ui_model.h"
static c6_ui_model_t m;
static taby_reusable_card_t card;
static void show(int kind, int width, int height, int64_t now) {
    card.kind = kind;
    assert(c6_ui_model_show(&m, &card, "UI_TEST", now, width, height));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    c6_ui_model_init(&m);
    int scenario = atoi(argv[1]);
    if (scenario == 1) {
        card.countdown_total_seconds = 10;
        card.countdown_remaining_seconds = 5;
        card.behavior.countdown_running = true;
        show(TABY_REUSABLE_CARD_COUNTDOWN, 172, 320, 1000000);
        assert(c6_ui_model_remaining(&m, 999999) == 5);
        assert(c6_ui_model_remaining(&m, 1999999) == 5);
        assert(c6_ui_model_remaining(&m, 2000000) == 4);
        assert(c6_ui_model_remaining(&m, 6000000) == 0);
        assert(c6_ui_model_remaining(&m, INT64_MAX) == 0);
        card.behavior.countdown_running = false;
        card.countdown_remaining_seconds = 4;
        show(TABY_REUSABLE_CARD_COUNTDOWN, 172, 320, 6000000);
        assert(c6_ui_model_remaining(&m, 90000000) == 4);
        card.behavior.countdown_running = true;
        show(TABY_REUSABLE_CARD_COUNTDOWN, 172, 320, 90000000);
        assert(c6_ui_model_remaining(&m, 91000000) == 3);
    } else if (scenario == 2) {
        card.progress_percent = 255;
        show(TABY_REUSABLE_CARD_PROGRESS, 172, 320, 0);
        assert(c6_ui_model_progress(&m, 0) == 100);
        card.progress_percent = 0;
        show(TABY_REUSABLE_CARD_PROGRESS, 172, 320, 0);
        assert(c6_ui_model_progress(&m, 0) == 0);
        card.countdown_total_seconds = UINT32_MAX;
        card.countdown_remaining_seconds = 1;
        show(TABY_REUSABLE_CARD_COUNTDOWN, 172, 320, 0);
        assert(c6_ui_model_progress(&m, 0) == 99);
        card.countdown_total_seconds = 0;
        show(TABY_REUSABLE_CARD_COUNTDOWN, 172, 320, 0);
        assert(c6_ui_model_progress(&m, 0) == 0);
        card.countdown_total_seconds = 5;
        card.countdown_remaining_seconds = 6;
        show(TABY_REUSABLE_CARD_COUNTDOWN, 172, 320, 0);
        assert(c6_ui_model_progress(&m, 0) == 0);
    } else if (scenario == 3) {
        char title[] = "Borrowed title";
        char subtitle[] = "Borrowed subtitle";
        card.title = title; card.subtitle = subtitle;
        show(TABY_REUSABLE_CARD_TEXT, 172, 320, 0);
        title[0] = 'X'; subtitle[0] = 'X';
        assert(strcmp(m.card.title, "Borrowed title") == 0);
        assert(strcmp(m.card.subtitle, "Borrowed subtitle") == 0);
        card.behavior.decor_effect = (taby_reusable_decor_effect_t)99;
        assert(!c6_ui_model_show(&m, &card, "UI_BAD", 1, 172, 320));
        assert(strcmp(m.state_name, "UI_TEST") == 0);
        assert(strcmp(m.card.title, "Borrowed title") == 0);
        card.behavior.decor_effect = TABY_REUSABLE_DECOR_EFFECT_NONE;
        card.behavior.animation_id = "greeting";
        assert(!c6_ui_model_show(&m, &card, "UI_BAD", 1, 172, 320));
        card.behavior.animation_id = NULL;
        char too_long[300]; memset(too_long, 'a', sizeof(too_long)); too_long[299] = 0;
        card.title = too_long;
        assert(!c6_ui_model_show(&m, &card, "UI_BAD", 1, 172, 320));
        assert(strcmp(m.card.title, "Borrowed title") == 0);
        assert(!c6_ui_model_show(&m, NULL, "BAD", 0, 172, 320));
        card.title = NULL;
        card.kind = TABY_REUSABLE_CARD_ICON_ONLY;
        card.behavior.text_effect = TABY_REUSABLE_TEXT_EFFECT_BLINK;
        assert(!c6_ui_model_show(&m, &card, "UI_BAD", 1, 172, 320));
    } else if (scenario == 4) {
        for (int orientation = 0; orientation < 2; ++orientation) {
            int w = orientation ? 320 : 172, h = orientation ? 172 : 320;
            show(TABY_REUSABLE_CARD_CHOICE_2, w, h, 1000000 + orientation * 3000000);
            for (int i = 0; i < 2; ++i) {
                c6_ui_rect_t r = m.buttons[i];
                assert(r.w >= 60 && r.h >= 32);
                assert(r.x >= 0 && r.y >= 0 && r.x+r.w <= w && r.y+r.h <= h);
                uint32_t before = m.choice.signal;
                int64_t t = 2000000 + orientation * 3000000 + i * 1000000;
                c6_ui_model_touch(&m, true, r.x+r.w/2, r.y+r.h/2, t);
                c6_ui_model_touch(&m, false, 0, 0, t+20000);
                assert(m.choice.signal == before+1);
                assert(m.choice.selection == (i ? TABY_REUSABLE_CHOICE_2 : TABY_REUSABLE_CHOICE_1));
            }
        }
        assert(m.choice.signal == 4);
        c6_ui_model_hide(&m);
        assert(!m.active && m.choice.signal == 4 && m.choice.selection == TABY_REUSABLE_CHOICE_NONE);
        c6_ui_model_touch(&m, true, 100, 100, 9000000);
        c6_ui_model_touch(&m, false, 100, 100, 9050000);
        assert(m.choice.signal == 4);
        show(TABY_REUSABLE_CARD_ACTION, 172, 320, 10000000);
        c6_ui_rect_t r = m.buttons[0];
        c6_ui_model_touch(&m, true, r.x, r.y, 11000000);
        c6_ui_model_touch(&m, false, 0, 0, 11020000);
        assert(m.choice.signal == 5 && m.choice.selection == TABY_REUSABLE_CHOICE_ACTION);
    } else if (scenario == 5) {
        show(TABY_REUSABLE_CARD_ACTION, 172, 320, 0);
        c6_ui_rect_t r = m.buttons[0];
        c6_ui_model_touch(&m, true, r.x, r.y, 1000000);
        c6_ui_model_touch(&m, false, 0, 0, 1019999);
        assert(m.choice.signal == 0);
        c6_ui_model_touch(&m, true, r.x+r.w, r.y, 2000000);
        c6_ui_model_touch(&m, false, 0, 0, 2050000);
        assert(m.choice.signal == 0);
        c6_ui_model_touch(&m, true, r.x, r.y, 3000000);
        c6_ui_model_touch(&m, false, 0, 0, 3700001);
        assert(m.choice.signal == 0);
        c6_ui_model_touch(&m, true, r.x, r.y, 4000000);
        c6_ui_model_touch(&m, false, 0, 0, 4020000);
        assert(m.choice.signal == 1);
        c6_ui_model_touch(&m, true, r.x, r.y, 4030000);
        c6_ui_model_touch(&m, false, 0, 0, 4050000);
        assert(m.choice.signal == 1);
        c6_ui_model_touch(&m, true, r.x, r.y, 4200000);
        c6_ui_model_hide(&m);
        show(TABY_REUSABLE_CARD_ACTION, 172, 320, 4210000);
        c6_ui_model_touch(&m, false, 0, 0, 4250000);
        assert(m.choice.signal == 1);
    } else if (scenario == 6) {
        card.behavior.countdown_visible_seconds = 2;
        show(TABY_REUSABLE_CARD_COUNTDOWN, 172, 320, 1000000);
        assert(!c6_ui_model_expired(&m, 2999999));
        assert(c6_ui_model_expired(&m, 3000000));
        card.behavior.countdown_visible_seconds = 0;
        card.title = "Loading";
        card.behavior.text_effect = TABY_REUSABLE_TEXT_EFFECT_LOADING_DOTS;
        card.behavior.text_effect_seconds = 1;
        show(TABY_REUSABLE_CARD_TEXT, 172, 320, 1000000);
        assert(c6_ui_model_effect_phase(&m, 1000000) == 0);
        assert(c6_ui_model_effect_phase(&m, 1360000) == 1);
        assert(c6_ui_model_effect_phase(&m, 1720000) == 2);
        assert(c6_ui_model_effect_phase(&m, 2000000) == -1);
    } else if (scenario == 7) {
        card.title = "Decorated";
        card.behavior.decor_effect_seconds = 2;
        for (int effect=1; effect<=24; ++effect) {
            card.behavior.decor_effect = effect;
            show(TABY_REUSABLE_CARD_HEADLINE, 172, 320, 1000000);
            show(TABY_REUSABLE_CARD_TEXT, 320, 172, 1000000);
        }
    } else if (scenario == 8) {
        /* Literal upstream normalization table, indexed by public enum. */
        const int families[] = {0,1,2,3,4,2,2,7,8,2,1,4,2,24,1,24,16,2,2,7,3,4,2,2,24};
        card.title = "Decorated";
        card.behavior.decor_effect_seconds = 2;
        c6_ui_decor_frame_t frame;
        for (int effect=1; effect<=24; ++effect) {
            assert((int)c6_ui_model_decor_family(effect) == families[effect]);
            card.behavior.decor_effect = effect;
            for (int rotation=0; rotation<2; ++rotation) {
                int w=rotation ? 320 : 172, h=rotation ? 172 : 320;
                show(TABY_REUSABLE_CARD_TEXT,w,h,1000000);
                for (int t=0;t<2000;t+=40) {
                    assert(c6_ui_model_decor_frame(&m,1000000+t*1000,&frame));
                    assert(frame.count>0 && frame.count<=8);
                    for(unsigned i=0;i<frame.count;++i) {
                        c6_ui_decor_shape_t *p=&frame.shapes[i];
                        assert(p->count>=2 && p->count<=C6_UI_DECOR_POINTS);
                        assert(p->width<=4);
                        if (families[effect]==2) {
                            int extent=0;
                            for(unsigned j=0;j<p->count;++j)
                                if(p->points[j].x>extent) extent=p->points[j].x;
                            assert(extent>=6); /* fireworks read at native size */
                        }
                        for(unsigned j=0;j<p->count;++j) {
                            assert(p->x+p->points[j].x>=2 && p->x+p->points[j].x<w-2);
                            assert(p->y+p->points[j].y>=2 && p->y+p->points[j].y<h-2);
                        }
                    }
                }
                assert(c6_ui_model_decor_frame(&m,2999999,&frame));
                c6_ui_model_resize(&m,h,w);
                assert(m.started_us==1000000);
                assert(!c6_ui_model_decor_frame(&m,3000000,&frame));
                assert(m.active && strcmp(m.card.title,"Decorated")==0);
            }
        }
        card.behavior.decor_effect = TABY_REUSABLE_DECOR_EFFECT_GOLD_RINGS;
        card.behavior.decor_effect_seconds = 0;
        show(TABY_REUSABLE_CARD_HEADLINE,172,320,1000000);
        assert(!c6_ui_model_decor_frame(&m,1000000,&frame));
        card.behavior.decor_effect_seconds = UINT32_MAX;
        show(TABY_REUSABLE_CARD_HEADLINE,172,320,1000000);
        int64_t end=1000000+(uint64_t)UINT32_MAX*1000000;
        assert(c6_ui_model_decor_frame(&m,end-1,&frame));
        assert(!c6_ui_model_decor_frame(&m,end,&frame));
        assert(!c6_ui_model_decor_frame(&m,INT64_MAX,&frame));
        assert(c6_ui_model_decor_frame(&m,999999,&frame));
        c6_ui_model_hide(&m);
        assert(!c6_ui_model_decor_frame(&m,1000000,&frame));
    } else if (scenario == 9) {
        card.title="Keep me";
        show(TABY_REUSABLE_CARD_TEXT,172,320,123);
        c6_ui_model_t original=m;
        for(int effect=-1; effect<=25; effect+=26) {
            card.behavior.decor_effect=effect;
            assert(!c6_ui_model_show(&m,&card,"BAD",1,172,320));
            assert(memcmp(&original,&m,sizeof(m))==0);
        }
        card.behavior.decor_effect=TABY_REUSABLE_DECOR_EFFECT_HEART_BURST;
        for (int kind=TABY_REUSABLE_CARD_ICON_ONLY;kind<=TABY_REUSABLE_CARD_PROGRESS;++kind) {
            card.kind=kind;
            assert(!c6_ui_model_show(&m,&card,"BAD",1,172,320));
            assert(memcmp(&original,&m,sizeof(m))==0);
        }
        card.kind=-1;
        assert(!c6_ui_model_show(&m,&card,"BAD",1,172,320));
        assert(memcmp(&original,&m,sizeof(m))==0);
    } else { assert(0); }
    return 0;
}
'''


class C6UiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        (folder / "ui_model_test.c").write_text(HARNESS)
        cls.binary = folder / "ui_model_test"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined", f"-I{CODE}", f"-I{SHARED}",
                        str(CODE / "c6_ui_model.c"), str(folder / "ui_model_test.c"),
                        "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def scenario(self, number):
        subprocess.run([str(self.binary), str(number)], check=True)

    def test_countdown_boundaries_pause_resume_and_large_elapsed(self):
        self.scenario(1)

    def test_progress_clamps_and_countdown_math_cannot_overflow(self):
        self.scenario(2)

    def test_owns_borrowed_text_and_rejects_unsupported_cards_atomically(self):
        self.scenario(3)

    def test_both_orientations_hit_visible_buttons_and_preserve_generations(self):
        self.scenario(4)

    def test_rejects_glitches_long_presses_outside_edges_and_stale_releases(self):
        self.scenario(5)

    def test_visibility_and_loading_dot_duration_boundaries(self):
        self.scenario(6)

    def test_all_decor_names_are_accepted_on_title_and_text(self):
        self.scenario(7)

    def test_decor_normalization_lifetime_geometry_and_orientation_epoch(self):
        self.scenario(8)

    def test_invalid_decor_and_ineligible_kinds_preserve_entire_model(self):
        self.scenario(9)


class C6UiIconsTests(unittest.TestCase):
    def test_reduction_preserves_coverage_nibble_order_and_rejects_bad_size(self):
        spec = importlib.util.spec_from_file_location("c6_ui_icons", ROOT / "tools/c6_ui_icons.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.reduce_alpha4(bytes(4608)), bytes(512))
        self.assertEqual(module.reduce_alpha4(bytes([255])*4608), bytes([255])*512)
        source = bytearray(4608)
        source[0] = 0xf0  # one of nine pixels: rounded coverage 2/15
        for y in range(3):
            for x in range(3, 6):  # adjacent output pixel: full coverage
                i = y*96+x
                source[i//2] |= 15 << (4 if i%2 == 0 else 0)
        self.assertEqual(module.reduce_alpha4(source)[:2], b"\x2f\x00")
        with self.assertRaises(ValueError):
            module.reduce_alpha4(b"bad")
        subprocess.run(["python3", str(ROOT / "tools/c6_ui_icons.py"), "--check"], check=True)


LVGL = ROOT / "firmware/targets/c6-lcd-1.47/managed_components/lvgl__lvgl"


@unittest.skipUnless((LVGL / "lvgl.h").exists(), "Install target LVGL dependency for renderer integration")
class C6UiRendererTests(unittest.TestCase):
    def test_real_parser_and_lvgl_render_both_geometries_and_retain_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            config = (LVGL / "lv_conf_template.h").read_text().replace("#if 0 /*Set", "#if 1 /*Set", 1)
            config = config.replace("#define LV_COLOR_16_SWAP 0", "#define LV_COLOR_16_SWAP 1")
            for size in (12, 16, 20, 28):
                config = re.sub(rf"(#define LV_FONT_MONTSERRAT_{size}) 0\b", r"\1 1", config)
            (folder / "lv_conf.h").write_text(config)
            (folder / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n")
            (folder / "esp_timer.h").write_text("#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
            (folder / "esp_heap_caps.h").write_text("#include <stdlib.h>\n#define MALLOC_CAP_DMA 1\n#define MALLOC_CAP_INTERNAL 2\n#define heap_caps_malloc(size,caps) malloc(size)\n#define heap_caps_free(ptr) free(ptr)\n")
            (folder / "esp_log.h").write_text("#include <stdio.h>\n#define ESP_LOGE(tag,...) do { (void)(tag); fprintf(stderr,__VA_ARGS__); fputc('\\n',stderr); } while(0)\n#define ESP_LOGW ESP_LOGE\n#define ESP_LOGI(tag,...) do { (void)(tag); } while(0)\n")
            common = ["-std=c11", "-DLV_CONF_INCLUDE_SIMPLE", f"-I{folder}", f"-I{CODE}", f"-I{SHARED}", f"-I{LVGL}"]
            # Our adapter is warning-clean; the dependency retains its own warning policy.
            subprocess.run(["cc", *common, "-Wall", "-Wextra", "-Werror", "-fsyntax-only", str(CODE / "c6_ui.c")], check=True)
            binary = folder / "ui_renderer_test"
            subprocess.run(["cc", *common, "-fsanitize=undefined", "-O1", str(CODE / "c6_ui.c"),
                            str(CODE / "c6_ui_model.c"), str(ROOT / "tests/c6_ui_host.c"),
                            str(SHARED / "taby_reusable_preview.c"),
                            str(SHARED / "generated/taby_reusable_icons.c"),
                            *map(str, sorted((LVGL / "src").rglob("*.c"))), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary), *([os.environ["C6_UI_SCREENSHOTS"]] if "C6_UI_SCREENSHOTS" in os.environ else [])], check=True)


if __name__ == "__main__":
    unittest.main()
