#pragma once

#include "taby_reusable_ui.h"

/* Fixed-capacity owned state. Capacities match the upstream preview parser. */
typedef struct { int x, y, w, h; } c6_ui_rect_t;
typedef struct {
    bool active;
    taby_reusable_card_t card;
    char headline[48], title[96], subtitle[128], icon_id[24];
    char primary_action_label[48], secondary_action_label[48], state_name[48];
    int width, height;
    int64_t started_us;
    c6_ui_rect_t buttons[2];
    taby_reusable_choice_signal_t choice;
    bool pressed, tapped;
    uint16_t touch_x, touch_y;
    taby_reusable_choice_selection_t pressed_selection;
    int64_t pressed_us, last_tap_us;
} c6_ui_model_t;

void c6_ui_model_init(c6_ui_model_t *model);
bool c6_ui_model_show(c6_ui_model_t *model, const taby_reusable_card_t *card,
                      const char *state_name, int64_t now_us, int width, int height);
void c6_ui_model_hide(c6_ui_model_t *model);
void c6_ui_model_resize(c6_ui_model_t *model, int width, int height);
uint32_t c6_ui_model_remaining(const c6_ui_model_t *model, int64_t now_us);
uint8_t c6_ui_model_progress(const c6_ui_model_t *model, int64_t now_us);
bool c6_ui_model_expired(const c6_ui_model_t *model, int64_t now_us);
/* -1 means stopped; otherwise blink visibility or loading-dot count 0..3. */
int c6_ui_model_effect_phase(const c6_ui_model_t *model, int64_t now_us);
void c6_ui_model_touch(c6_ui_model_t *model, bool pressed, uint16_t x, uint16_t y,
                       int64_t now_us);

/* Bounded, allocation-free decoration geometry in local object coordinates. */
#define C6_UI_DECOR_OBJECTS 8
#define C6_UI_DECOR_POINTS 17
typedef struct { int16_t x, y; } c6_ui_decor_point_t;
typedef struct {
    int x, y;
    uint32_t color;
    uint8_t count, width, opacity;
    c6_ui_decor_point_t points[C6_UI_DECOR_POINTS];
} c6_ui_decor_shape_t;
typedef struct {
    unsigned count;
    c6_ui_decor_shape_t shapes[C6_UI_DECOR_OBJECTS];
} c6_ui_decor_frame_t;
taby_reusable_decor_effect_t c6_ui_model_decor_family(taby_reusable_decor_effect_t effect);
bool c6_ui_model_decor_frame(const c6_ui_model_t *model, int64_t now_us,
                             c6_ui_decor_frame_t *frame);
