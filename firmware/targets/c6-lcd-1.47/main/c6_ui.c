#include "c6_ui.h"
#include "c6_ui_model.h"
#include "c6_board.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "generated/taby_reusable_icons.h"
#include "generated/c6_ui_icons.h"

_Static_assert(sizeof(lv_color_t) == 2, "C6 requires 16-bit RGB565 LVGL colors");

static const char *TAG = "c6_ui";
static c6_ui_model_t s_model;
static lv_disp_draw_buf_t s_draw_buffer;
static lv_disp_drv_t s_display_driver;
static lv_indev_drv_t s_input_driver;
static lv_disp_t *s_display;
static lv_obj_t *s_root, *s_primary, *s_value, *s_bar;
static bool s_initialized, s_touch_pressed, s_flush_failed;
static uint16_t s_touch_x, s_touch_y;
static int64_t s_tick_us;
static int s_effect_phase = -2;
static uint32_t s_last_remaining = UINT32_MAX;
static char s_primary_base[128];
static char s_effect_text[132], s_value_text[32];
/* Alpha-only LVGL images cannot be transformed. Expand one small icon to
   RGB565+alpha before zooming, never a full-screen canvas. */
static uint8_t s_icon_pixels[32U * 32U * LV_IMG_PX_SIZE_ALPHA_BYTE];
static lv_img_dsc_t s_icon_image;
/* Eight small outlines; backing points remain valid for LVGL between ticks. */
static lv_obj_t *s_decor[C6_UI_DECOR_OBJECTS];
static lv_point_t s_decor_points[C6_UI_DECOR_OBJECTS][C6_UI_DECOR_POINTS];
static c6_ui_decor_frame_t s_decor_frame;

static void update_decor(int64_t now) {
    bool active = c6_ui_model_decor_frame(&s_model, now, &s_decor_frame);
    for (unsigned i=0; i<C6_UI_DECOR_OBJECTS; ++i) {
        if (!s_decor[i]) continue;
        if (!active || i>=s_decor_frame.count) {
            lv_obj_add_flag(s_decor[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const c6_ui_decor_shape_t *p = &s_decor_frame.shapes[i];
        lv_obj_clear_flag(s_decor[i], LV_OBJ_FLAG_HIDDEN);
        for (unsigned j=0; j<p->count; ++j) {
            s_decor_points[i][j].x = p->points[j].x;
            s_decor_points[i][j].y = p->points[j].y;
        }
        lv_line_set_points(s_decor[i], s_decor_points[i], p->count);
        lv_obj_set_pos(s_decor[i], p->x, p->y);
        lv_obj_set_style_line_color(s_decor[i], lv_color_hex(p->color), 0);
        lv_obj_set_style_line_width(s_decor[i], p->width, 0);
        lv_obj_set_style_line_opa(s_decor[i], p->opacity, 0);
    }
}

static void create_decor(int64_t now) {
    if (!c6_ui_model_decor_frame(&s_model, now, &s_decor_frame)) return;
    for (unsigned i=0; i<s_decor_frame.count; ++i) {
        s_decor[i] = lv_line_create(s_root);
        lv_obj_remove_style_all(s_decor[i]);
        lv_obj_clear_flag(s_decor[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    update_decor(now);
}

static int clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

static bool has_text(const char *text) { return text && text[0]; }

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    esp_err_t result = c6_board_draw_rect(area->x1, area->y1,
        area->x2-area->x1+1, area->y2-area->y1+1, (const uint8_t *)pixels);
    if (result != ESP_OK) {
        if (!s_flush_failed) ESP_LOGE(TAG, "LCD rectangle failed: %d", (int)result);
        s_flush_failed = true;
    }
    /* The board stages/copies the input and returns safe caller ownership
       even on a transfer error; LVGL may reuse its draw buffer now. */
    lv_disp_flush_ready(driver);
}

static void read_touch(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver;
    data->state = s_model.active && s_touch_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = s_touch_x;
    data->point.y = s_touch_y;
    data->continue_reading = false;
}

void c6_ui_init(void) {
    if (s_initialized) return;
    s_initialized = true;
    c6_ui_model_init(&s_model);
    lv_init();
    lv_color_t *pixels = heap_caps_malloc(320U * 10U * sizeof(lv_color_t),
                                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!pixels) { ESP_LOGE(TAG, "No memory for LCD draw buffer"); return; }
    lv_disp_draw_buf_init(&s_draw_buffer, pixels, NULL, 320U * 10U);
    lv_disp_drv_init(&s_display_driver);
    s_display_driver.hor_res = c6_board_width();
    s_display_driver.ver_res = c6_board_height();
    s_display_driver.draw_buf = &s_draw_buffer;
    s_display_driver.flush_cb = flush;
    s_display = lv_disp_drv_register(&s_display_driver);
    if (!s_display) {
        ESP_LOGE(TAG, "LVGL display registration failed");
        heap_caps_free(pixels);
        return;
    }
    lv_indev_drv_init(&s_input_driver);
    s_input_driver.type = LV_INDEV_TYPE_POINTER;
    s_input_driver.disp = s_display;
    s_input_driver.read_cb = read_touch;
    if (!lv_indev_drv_register(&s_input_driver)) {
        ESP_LOGE(TAG, "LVGL touch registration failed");
        lv_disp_remove(s_display);
        heap_caps_free(pixels);
        s_display = NULL;
        return;
    }
    s_tick_us = esp_timer_get_time();
}

static const lv_font_t *font_for(int base, unsigned scale) {
    if (!scale) scale = 100;
    unsigned size = (unsigned)base * scale / 100U;
    if (size <= 13) return &lv_font_montserrat_12;
    if (size <= 17) return &lv_font_montserrat_16;
    if (size <= 23) return &lv_font_montserrat_20;
    return &lv_font_montserrat_28;
}

static lv_obj_t *label(const char *text, int x, int y, int width, int height,
                       const lv_font_t *font, uint32_t color) {
    lv_obj_t *obj = lv_label_create(s_root);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_label_set_text(obj, text ? text : "");
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(obj, 2, 0);
    lv_point_t measured;
    lv_txt_get_size(&measured, text ? text : "", font, 0, 2, width, LV_TEXT_FLAG_NONE);
    if (measured.y > height) {
        font = &lv_font_montserrat_12;
        lv_obj_set_style_text_font(obj, font, 0);
        lv_txt_get_size(&measured, text ? text : "", font, 0, 2, width, LV_TEXT_FLAG_NONE);
        if (measured.y > height) {
            lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
            measured.y = font->line_height;
        }
    }
    if (measured.y > 0 && measured.y < height) {
        lv_obj_set_y(obj, y+(height-measured.y)/2);
        lv_obj_set_height(obj, measured.y);
    }
    return obj;
}

/* S3 offsets describe a rotated 456x280 canvas. On C6 they are bounded,
   quarter-strength adjustments to responsive slots, rather than pixel parity. */
static lv_obj_t *positioned_label(const char *text, int y, int height,
                                   int base_size, unsigned scale,
                                   int offset_x, int offset_y, uint32_t color) {
    int w = s_model.width, h = s_model.height;
    int x = clamp(12 + offset_x*w/1824, 8, w/3);
    y = clamp(y + offset_y*h/1120, 8, h-20);
    int bottom = (s_model.card.kind == TABY_REUSABLE_CARD_ACTION ||
                  s_model.card.kind == TABY_REUSABLE_CARD_CHOICE_2) ? s_model.buttons[0].y-4 : h-8;
    height = clamp(height, 16, bottom-12);
    y = clamp(y, 8, bottom-height);
    return label(text, x, y, w-x-8, height, font_for(base_size, scale), color);
}

static void draw_icon(const lv_img_dsc_t *image, int x, int y, int size) {
    if (!image) return;
    s_icon_image = (lv_img_dsc_t){
        .header = {.always_zero=0, .w=32, .h=32, .cf=LV_IMG_CF_TRUE_COLOR_ALPHA},
        .data_size=sizeof(s_icon_pixels), .data=s_icon_pixels,
    };
    lv_color_t color = lv_color_hex(s_model.card.style.accent_hex);
    for (unsigned i = 0; i < 32U*32U; ++i) {
        memcpy(s_icon_pixels+i*LV_IMG_PX_SIZE_ALPHA_BYTE, &color, sizeof(color));
        unsigned alpha = (image->data[i/2] >> (i%2 ? 0 : 4)) & 15U;
        s_icon_pixels[i*LV_IMG_PX_SIZE_ALPHA_BYTE+sizeof(color)] = (uint8_t)(alpha*17U);
    }
    lv_obj_t *icon = lv_img_create(s_root);
    lv_img_set_src(icon, &s_icon_image);
    lv_img_set_zoom(icon, (uint16_t)(size*256/32));
    lv_img_set_pivot(icon, 0, 0);
    lv_obj_set_pos(icon, x, y);
    lv_obj_set_style_img_recolor(icon, lv_color_hex(s_model.card.style.accent_hex), 0);
    lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, 0);
}

static void button(int index, const char *text, bool secondary) {
    c6_ui_rect_t r = s_model.buttons[index];
    if (!r.w) return;
    const taby_reusable_style_t *s = &s_model.card.style;
    lv_obj_t *obj = lv_obj_create(s_root);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, r.x, r.y);
    lv_obj_set_size(obj, r.w, r.h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(secondary ? s->secondary_button_fill_hex : s->button_fill_hex), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(secondary ? s->secondary_button_border_hex : s->button_border_hex), 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    /* Input uses the same bounded rectangles as these visuals, independently
       of the LVGL polling interval (short taps must not get lost). */
    label(text, r.x+5, r.y+5, r.w-10, r.h-8, font_for(16, 100),
          secondary ? s->secondary_button_text_hex : s->button_text_hex);
}

static void format_remaining(char *buffer, size_t size, uint32_t seconds) {
    if (seconds >= 3600U) {
        snprintf(buffer, size, "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
                 seconds/3600U, (seconds/60U)%60U, seconds%60U);
    } else {
        snprintf(buffer, size, "%02" PRIu32 ":%02" PRIu32, seconds/60U, seconds%60U);
    }
}

static void update_dynamic(int64_t now) {
    update_decor(now);
    const taby_reusable_card_t *c = &s_model.card;
    uint32_t remaining = c6_ui_model_remaining(&s_model, now);
    if (s_value && c->kind == TABY_REUSABLE_CARD_COUNTDOWN && remaining != s_last_remaining) {
        format_remaining(s_value_text, sizeof(s_value_text), remaining);
        lv_label_set_text_static(s_value, s_value_text);
        s_last_remaining = remaining;
    }
    if (s_bar) lv_bar_set_value(s_bar, c6_ui_model_progress(&s_model, now), LV_ANIM_OFF);
    int phase = c6_ui_model_effect_phase(&s_model, now);
    if (!s_primary || phase == s_effect_phase) return;
    s_effect_phase = phase;
    lv_obj_clear_flag(s_primary, LV_OBJ_FLAG_HIDDEN);
    if (c->behavior.text_effect == TABY_REUSABLE_TEXT_EFFECT_BLINK) {
        if (phase == 1) lv_obj_add_flag(s_primary, LV_OBJ_FLAG_HIDDEN);
    } else if (c->behavior.text_effect == TABY_REUSABLE_TEXT_EFFECT_LOADING_DOTS) {
        unsigned dots = phase < 0 ? 3U : (unsigned)phase;
        snprintf(s_effect_text, sizeof(s_effect_text), "%s%.*s", s_primary_base, (int)dots, "...");
        lv_label_set_text_static(s_primary, s_effect_text);
    }
}

static void build_card(const lv_img_dsc_t *image) {
    if (s_root) lv_obj_del(s_root);
    s_primary = s_value = s_bar = NULL;
    memset(s_decor, 0, sizeof(s_decor));
    s_effect_phase = -2;
    s_last_remaining = UINT32_MAX;
    const taby_reusable_card_t *c = &s_model.card;
    const taby_reusable_style_t *s = &c->style;
    const taby_reusable_layout_t *l = &c->layout;
    int w = s_model.width, h = s_model.height;
    bool landscape = w > h;
    bool interactive = c->kind == TABY_REUSABLE_CARD_ACTION || c->kind == TABY_REUSABLE_CARD_CHOICE_2;
    bool meter = c->kind == TABY_REUSABLE_CARD_COUNTDOWN || c->kind == TABY_REUSABLE_CARD_PROGRESS;
    int top = has_text(c->headline) ? 36 : 14;
    int bottom = interactive ? s_model.buttons[0].y-6 : h-12;
    int available = bottom-top;
    s_root = lv_obj_create(lv_disp_get_scr_act(s_display));
    lv_obj_remove_style_all(s_root);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_size(s_root, w, h);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(s->background_hex), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(s_root);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, 4, 4);
    lv_obj_set_size(panel, w-8, h-8);
    lv_obj_set_style_bg_color(panel, lv_color_hex(s->card_hex), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(s->panel_border_hex), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 9, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    create_decor(esp_timer_get_time());
    if (has_text(c->headline)) label(c->headline, 12, 12, w-24, 22, font_for(12, 100), s->headline_hex);

    int title_y = top, title_h = landscape ? 42 : 62;
    int subtitle_y = bottom-(landscape ? 34 : 62), subtitle_h = landscape ? 32 : 58;
    if (image) {
        int max_icon = landscape ? 64 : 88;
        if (c->kind == TABY_REUSABLE_CARD_ICON_ONLY) max_icon = landscape ? 104 : 132;
        if (interactive) max_icon = landscape ? 36 : 60;
        if (!landscape && c->kind == TABY_REUSABLE_CARD_ICON_TEXT_ROW) max_icon = 40;
        int size = clamp(l->icon_size ? (int)l->icon_size * (landscape ? h : w) / 280 : max_icon,
                         24, max_icon);
        int icon_x = (w-size)/2;
        int icon_y = top+(available-size)/2;
        if (c->kind == TABY_REUSABLE_CARD_ICON_TEXT_ROW || (landscape && c->kind != TABY_REUSABLE_CARD_ICON_ONLY)) {
            icon_x = 16;
            icon_y = top+(available-size)/2;
        } else if (c->kind != TABY_REUSABLE_CARD_ICON_ONLY) {
            icon_y = top;
            title_y = top+size+6;
        }
        icon_x = clamp(icon_x + l->icon_offset_x*w/1824, 8, w-size-8);
        icon_y = clamp(icon_y + l->icon_offset_y*h/1120, 8, bottom-size);
        draw_icon(image, icon_x, icon_y, size);
        if (landscape || c->kind == TABY_REUSABLE_CARD_ICON_TEXT_ROW) {
            int text_x = clamp(icon_x+size+8+l->title_offset_x*w/1824, icon_x+size+6, w-40);
            int text_h = clamp(available-12, 20, 64);
            int text_y = clamp(icon_y+(size-text_h)/2+l->title_offset_y*h/1120,
                               top, bottom-text_h);
            s_primary = label(c->title, text_x, text_y, clamp(w-text_x-12, 32, w),
                              text_h, font_for(landscape ? 20 : 16, l->title_scale_percent), s->title_hex);
        }
    }

    if (meter) {
        s_primary = positioned_label(c->title, top, landscape ? 28 : 48,
            16, l->title_scale_percent, l->title_offset_x, l->title_offset_y, s->title_hex);
        char value[32];
        if (c->kind == TABY_REUSABLE_CARD_COUNTDOWN) format_remaining(value, sizeof(value), c6_ui_model_remaining(&s_model, esp_timer_get_time()));
        else snprintf(value, sizeof(value), "%u%%", c6_ui_model_progress(&s_model, esp_timer_get_time()));
        s_value = positioned_label(value, top+available/3, 40, 28, l->value_scale_percent,
                                   l->value_offset_x, l->value_offset_y, s->title_hex);
        s_bar = lv_bar_create(s_root);
        lv_obj_set_pos(s_bar, 16, clamp(top+available*2/3+l->bar_offset_y*h/280, top+40, h-24));
        lv_obj_set_size(s_bar, w-32, 10);
        lv_bar_set_range(s_bar, 0, 100);
        lv_obj_set_style_bg_color(s_bar, lv_color_hex(s->progress_track_hex), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_bar, lv_color_hex(s->progress_fill_hex), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_anim_time(s_bar, 0, 0);
        subtitle_y = h-(landscape ? 34 : 62);
    } else if (c->kind != TABY_REUSABLE_CARD_ICON_ONLY && !s_primary) {
        if (!image) title_y = has_text(c->subtitle) ? top+available/5 : top+(available-title_h)/2;
        int dx = c->kind == TABY_REUSABLE_CARD_CHOICE_2 ? l->choice_title_offset_x : l->title_offset_x;
        int dy = c->kind == TABY_REUSABLE_CARD_CHOICE_2 ? l->choice_title_offset_y : l->title_offset_y;
        unsigned scale = c->kind == TABY_REUSABLE_CARD_CHOICE_2 ? l->choice_title_scale_percent : l->title_scale_percent;
        s_primary = positioned_label(c->title, title_y, title_h, 20, scale, dx, dy, s->title_hex);
    }
    if (has_text(c->subtitle) && c->kind != TABY_REUSABLE_CARD_ICON_ONLY) {
        if (interactive) { subtitle_y = bottom-36; subtitle_h = 32; }
        positioned_label(c->subtitle, subtitle_y, subtitle_h, 12, l->subtitle_scale_percent,
                         l->subtitle_offset_x, l->subtitle_offset_y, s->body_hex);
    }
    if (interactive) {
        button(0, has_text(c->primary_action_label) ? c->primary_action_label : "OK", false);
        if (c->kind == TABY_REUSABLE_CARD_CHOICE_2)
            button(1, has_text(c->secondary_action_label) ? c->secondary_action_label : "Cancel", true);
    }
    snprintf(s_primary_base, sizeof(s_primary_base), "%s", c->title ? c->title : "");
    if (c->behavior.text_effect == TABY_REUSABLE_TEXT_EFFECT_LOADING_DOTS) {
        /* Replace a supplied trailing ellipsis, as the original renderer does. */
        size_t length = strlen(s_primary_base);
        while (length && s_primary_base[length-1] == '.') s_primary_base[--length] = '\0';
    }
    update_dynamic(esp_timer_get_time());
    lv_obj_invalidate(s_root);
}

static const lv_img_dsc_t *lookup_icon(const char *id) {
    taby_reusable_icon_asset_t asset;
    return taby_reusable_icon_lookup(id, &asset) ? c6_ui_icon_image(asset.asset_pack_path) : NULL;
}

bool taby_runtime_render_reusable_card(const taby_reusable_card_t *card, const char *state_name) {
    c6_ui_init();
    if (!s_display || !card) return false;
    const lv_img_dsc_t *image = has_text(card->icon_id) ? lookup_icon(card->icon_id) : NULL;
    if ((card->kind >= TABY_REUSABLE_CARD_ICON_ONLY &&
         card->kind <= TABY_REUSABLE_CARD_ICON_TEXT_SUBTITLE && !image) ||
        (has_text(card->icon_id) && !image)) {
        ESP_LOGW(TAG, "Unsupported or missing icon: %s", card->icon_id ? card->icon_id : "");
        return false;
    }
    if (!c6_ui_model_show(&s_model, card, state_name, esp_timer_get_time(),
                           c6_board_width(), c6_board_height())) {
        ESP_LOGW(TAG, "Card rejected: invalid data or unsupported clip behavior");
        return false;
    }
    s_touch_pressed = false;
    build_card(image);
    s_flush_failed = false;
    lv_refr_now(s_display);
    if (s_flush_failed) { c6_ui_hide(); return false; }
    return true;
}

void c6_ui_tick(int64_t now_us) {
    if (!s_initialized || !s_display) return;
    if (now_us > s_tick_us) {
        uint64_t milliseconds = ((uint64_t)now_us-(uint64_t)s_tick_us)/1000U;
        lv_tick_inc((uint32_t)milliseconds);
        s_tick_us += (int64_t)(milliseconds*1000U);
    }
    if (!s_model.active) return;
    if (c6_ui_model_expired(&s_model, now_us)) { c6_ui_hide(); return; }
    update_dynamic(now_us);
    s_flush_failed = false;
    lv_timer_handler();
    if (s_flush_failed) c6_ui_hide();
}

void c6_ui_hide(void) {
    c6_ui_model_hide(&s_model);
    s_touch_pressed = false;
    if (s_root) lv_obj_del(s_root);
    s_root = s_primary = s_value = s_bar = NULL;
    memset(s_decor, 0, sizeof(s_decor));
    if (s_display) {
        /* Portrait clips leave twenty pixels above and below their image.
           Clear the entire LCD before handing it back to the clip player. */
        lv_obj_t *screen = lv_disp_get_scr_act(s_display);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_invalidate(screen);
        lv_refr_now(s_display);
    }
}

bool c6_ui_active(void) { return s_model.active; }

const char *c6_ui_state_name(void) { return s_model.active ? s_model.state_name : NULL; }

void c6_ui_touch(bool pressed, uint16_t x, uint16_t y) {
    s_touch_pressed = pressed && s_model.active;
    if (pressed) { s_touch_x = x; s_touch_y = y; }
    c6_ui_model_touch(&s_model, pressed, x, y, esp_timer_get_time());
}

void c6_ui_refresh_orientation(void) {
    if (!s_display) return;
    s_display_driver.hor_res = c6_board_width();
    s_display_driver.ver_res = c6_board_height();
    lv_disp_drv_update(s_display, &s_display_driver);
    s_touch_pressed = false;
    if (!s_model.active) return;
    c6_ui_model_resize(&s_model, c6_board_width(), c6_board_height());
    build_card(lookup_icon(s_model.card.icon_id));
    s_flush_failed = false;
    lv_refr_now(s_display);
    if (s_flush_failed) c6_ui_hide();
}

taby_reusable_style_t taby_reusable_ui_default_style(void) {
    return (taby_reusable_style_t){
        .background_hex=0x000000, .card_hex=0x161A21, .panel_border_hex=0x6C7684,
        .accent_hex=0x6C7684, .headline_hex=0xD7DEE7, .title_hex=0xFFFFFF,
        .body_hex=0xD7DEE7, .button_fill_hex=0x18242F, .button_text_hex=0xF7F7F5,
        .button_border_hex=0x6B8CAB, .secondary_button_fill_hex=0x1F232A,
        .secondary_button_text_hex=0xF7F7F5, .secondary_button_border_hex=0x9DA7B8,
        .progress_track_hex=0x27303B, .progress_fill_hex=0x6B8CAB,
    };
}

void taby_reusable_ui_read_choice_signal(taby_reusable_choice_signal_t *signal) {
    if (signal) *signal = s_model.choice;
}

const char *taby_reusable_ui_choice_selection_name(taby_reusable_choice_selection_t selection) {
    switch (selection) {
        case TABY_REUSABLE_CHOICE_1: return "choice_1";
        case TABY_REUSABLE_CHOICE_2: return "choice_2";
        case TABY_REUSABLE_CHOICE_ACTION: return "action";
        default: return "none";
    }
}
