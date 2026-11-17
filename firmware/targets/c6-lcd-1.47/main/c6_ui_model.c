#include "c6_ui_model.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static int clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

static bool geometry_valid(int width, int height) {
    return (width == 172 && height == 320) || (width == 320 && height == 172);
}

static uint64_t elapsed(const c6_ui_model_t *m, int64_t now) {
    return now > m->started_us ? (uint64_t)now - (uint64_t)m->started_us : 0;
}

static bool fits(const char *text, size_t capacity) {
    if (!text) return true;
    for (size_t i = 0; i < capacity; ++i) if (text[i] == '\0') return true;
    return false;
}

static const char *copy(char *destination, const char *source) {
    if (!source) { destination[0] = '\0'; return NULL; }
    memmove(destination, source, strlen(source) + 1U);
    return destination;
}

static void bind_owned_strings(c6_ui_model_t *m) {
    m->card.headline = m->headline[0] ? m->headline : NULL;
    m->card.title = m->title[0] ? m->title : NULL;
    m->card.subtitle = m->subtitle[0] ? m->subtitle : NULL;
    m->card.icon_id = m->icon_id[0] ? m->icon_id : NULL;
    m->card.primary_action_label = m->primary_action_label[0] ? m->primary_action_label : NULL;
    m->card.secondary_action_label = m->secondary_action_label[0] ? m->secondary_action_label : NULL;
    m->card.behavior.animation_id = NULL;
}

void c6_ui_model_init(c6_ui_model_t *m) {
    if (m) memset(m, 0, sizeof(*m));
}

void c6_ui_model_hide(c6_ui_model_t *m) {
    if (!m) return;
    m->active = false;
    m->pressed = false;
    m->choice.selection = TABY_REUSABLE_CHOICE_NONE;
    memset(m->buttons, 0, sizeof(m->buttons));
}

void c6_ui_model_resize(c6_ui_model_t *m, int width, int height) {
    if (!m || !geometry_valid(width, height)) return;
    m->width = width;
    m->height = height;
    m->pressed = false;
    memset(m->buttons, 0, sizeof(m->buttons));
    const taby_reusable_layout_t *l = &m->card.layout;
    bool choice = m->card.kind == TABY_REUSABLE_CARD_CHOICE_2;
    if (!choice && m->card.kind != TABY_REUSABLE_CARD_ACTION) return;
    int delta = choice ? l->choice_button_height_delta : l->action_button_height_delta;
    int offset = choice ? l->choice_button_offset_y : l->action_button_offset_y;
    int button_h = clamp(40 + delta * height / 280, 34, height / 4);
    int bottom = clamp(height - 12 - button_h + offset * height / 280,
                       height / 2, height - 8 - button_h);
    int button_w = width - 24;
    if (!choice) button_w = clamp(button_w + l->action_button_width_delta * width / 456, 80, width-16);
    if (choice && width < height) {
        /* Stack in portrait so two long action labels stay readable. */
        bottom = clamp(bottom, height / 2, height - 16 - button_h*2);
        m->buttons[0] = (c6_ui_rect_t){12, bottom, button_w, button_h};
        m->buttons[1] = (c6_ui_rect_t){12, bottom+button_h+8, button_w, button_h};
    } else if (choice) {
        button_w = (width - 32) / 2;
        m->buttons[0] = (c6_ui_rect_t){12, bottom, button_w, button_h};
        m->buttons[1] = (c6_ui_rect_t){20+button_w, bottom, button_w, button_h};
    } else {
        m->buttons[0] = (c6_ui_rect_t){(width-button_w)/2, bottom, button_w, button_h};
    }
}

bool c6_ui_model_show(c6_ui_model_t *m, const taby_reusable_card_t *card,
                      const char *state, int64_t now, int width, int height) {
    if (!m || !card || !geometry_valid(width, height) ||
        now < 0 || card->kind < TABY_REUSABLE_CARD_HEADLINE || card->kind > TABY_REUSABLE_CARD_PROGRESS ||
        card->behavior.text_effect < TABY_REUSABLE_TEXT_EFFECT_NONE ||
        card->behavior.text_effect > TABY_REUSABLE_TEXT_EFFECT_LOADING_DOTS ||
        (card->behavior.text_effect != TABY_REUSABLE_TEXT_EFFECT_NONE &&
         (card->kind == TABY_REUSABLE_CARD_ICON_ONLY || !card->title || !card->title[0])) ||
        card->behavior.decor_effect < TABY_REUSABLE_DECOR_EFFECT_NONE ||
        card->behavior.decor_effect > TABY_REUSABLE_DECOR_EFFECT_AURORA_SWEEP ||
        (card->behavior.decor_effect != TABY_REUSABLE_DECOR_EFFECT_NONE &&
         card->kind != TABY_REUSABLE_CARD_HEADLINE && card->kind != TABY_REUSABLE_CARD_TEXT) ||
        (card->behavior.animation_id && card->behavior.animation_id[0]) ||
        card->behavior.animation_play_on_show || card->behavior.animation_replay_seconds ||
        !fits(card->headline, sizeof(m->headline)) || !fits(card->title, sizeof(m->title)) ||
        !fits(card->subtitle, sizeof(m->subtitle)) || !fits(card->icon_id, sizeof(m->icon_id)) ||
        !fits(card->primary_action_label, sizeof(m->primary_action_label)) ||
        !fits(card->secondary_action_label, sizeof(m->secondary_action_label)) ||
        !fits(state, sizeof(m->state_name))) return false;
    /* Prepare a separate candidate: rejected requests leave the visible card intact,
       and the source may itself borrow strings from the current model. */
    c6_ui_model_t candidate = *m;
    candidate.card = *card;
    copy(candidate.headline, card->headline);
    copy(candidate.title, card->title);
    copy(candidate.subtitle, card->subtitle);
    copy(candidate.icon_id, card->icon_id);
    copy(candidate.primary_action_label, card->primary_action_label);
    copy(candidate.secondary_action_label, card->secondary_action_label);
    copy(candidate.state_name, state ? state : "UI_ACTIVE");
    candidate.started_us = now;
    candidate.active = true;
    candidate.pressed = false;
    candidate.choice.selection = TABY_REUSABLE_CHOICE_NONE;
    *m = candidate;
    bind_owned_strings(m);
    c6_ui_model_resize(m, width, height);
    return true;
}

uint32_t c6_ui_model_remaining(const c6_ui_model_t *m, int64_t now) {
    if (!m) return 0;
    uint32_t base = m->card.countdown_remaining_seconds;
    if (!m->card.behavior.countdown_running) return base;
    uint64_t seconds = elapsed(m, now) / 1000000U;
    return seconds >= base ? 0 : base - (uint32_t)seconds;
}

uint8_t c6_ui_model_progress(const c6_ui_model_t *m, int64_t now) {
    if (!m) return 0;
    if (m->card.kind != TABY_REUSABLE_CARD_COUNTDOWN)
        return m->card.progress_percent > 100U ? 100U : m->card.progress_percent;
    uint32_t total = m->card.countdown_total_seconds;
    uint32_t remaining = c6_ui_model_remaining(m, now);
    if (total == 0 || remaining > total) return 0;
    return (uint8_t)(((uint64_t)total - remaining) * 100U / total);
}

bool c6_ui_model_expired(const c6_ui_model_t *m, int64_t now) {
    return m && m->active && m->card.kind == TABY_REUSABLE_CARD_COUNTDOWN &&
        m->card.behavior.countdown_visible_seconds &&
        elapsed(m, now) >= (uint64_t)m->card.behavior.countdown_visible_seconds * 1000000U;
}

int c6_ui_model_effect_phase(const c6_ui_model_t *m, int64_t now) {
    if (!m || !m->active || m->card.behavior.text_effect == TABY_REUSABLE_TEXT_EFFECT_NONE) return -1;
    uint64_t age = elapsed(m, now);
    uint32_t seconds = m->card.behavior.text_effect_seconds;
    if (seconds && age >= (uint64_t)seconds * 1000000U) return -1;
    if (m->card.behavior.text_effect == TABY_REUSABLE_TEXT_EFFECT_BLINK)
        return (int)((age / 450000U) % 2U);
    return (int)((age / 360000U) % 4U);
}

static taby_reusable_choice_selection_t hit(const c6_ui_model_t *m, uint16_t x, uint16_t y) {
    for (int i = 0; i < 2; ++i) {
        c6_ui_rect_t r = m->buttons[i];
        if (r.w > 0 && x >= r.x && x < r.x+r.w && y >= r.y && y < r.y+r.h)
            return m->card.kind == TABY_REUSABLE_CARD_ACTION ? TABY_REUSABLE_CHOICE_ACTION :
                i ? TABY_REUSABLE_CHOICE_2 : TABY_REUSABLE_CHOICE_1;
    }
    return TABY_REUSABLE_CHOICE_NONE;
}

void c6_ui_model_touch(c6_ui_model_t *m, bool pressed, uint16_t x, uint16_t y, int64_t now) {
    if (!m || !m->active) return;
    if (pressed) {
        if (!m->pressed) {
            m->pressed_us = now;
            m->pressed_selection = hit(m, x, y);
        }
        m->pressed = true;
        m->touch_x = x; m->touch_y = y;
        return;
    }
    if (!m->pressed) return;
    m->pressed = false;
    if (now < m->pressed_us) return;
    uint64_t duration = (uint64_t)now - (uint64_t)m->pressed_us;
    taby_reusable_choice_selection_t selection = hit(m, m->touch_x, m->touch_y);
    if (duration < 20000U || duration > 700000U || selection == TABY_REUSABLE_CHOICE_NONE ||
        selection != m->pressed_selection ||
        (m->tapped && (now < m->last_tap_us || (uint64_t)now-(uint64_t)m->last_tap_us < 120000U))) return;
    m->tapped = true;
    m->last_tap_us = now;
    /* Saturate at the protocol's uint32 limit instead of making generations go backwards. */
    if (m->choice.signal < UINT32_MAX) ++m->choice.signal;
    m->choice.selection = selection;
}

taby_reusable_decor_effect_t c6_ui_model_decor_family(taby_reusable_decor_effect_t effect) {
    /* Keep this mapping identical to upstream normalized_decor_effect. */
    switch (effect) {
        case TABY_REUSABLE_DECOR_EFFECT_SHOOTING_STARS:
        case TABY_REUSABLE_DECOR_EFFECT_STAR_TWINKLE:
        case TABY_REUSABLE_DECOR_EFFECT_SUNBEAM_LINES:
            return TABY_REUSABLE_DECOR_EFFECT_SHOOTING_STARS;
        case TABY_REUSABLE_DECOR_EFFECT_COMET_BURST:
        case TABY_REUSABLE_DECOR_EFFECT_FLARE_FAN:
            return TABY_REUSABLE_DECOR_EFFECT_COMET_BURST;
        case TABY_REUSABLE_DECOR_EFFECT_FIREWORK_DOTS:
        case TABY_REUSABLE_DECOR_EFFECT_SQUARE_CONFETTI:
        case TABY_REUSABLE_DECOR_EFFECT_DIAMOND_BURST:
        case TABY_REUSABLE_DECOR_EFFECT_EMBER_DRIFT:
        case TABY_REUSABLE_DECOR_EFFECT_PIXEL_BURST:
        case TABY_REUSABLE_DECOR_EFFECT_CONFETTI_RAIN:
        case TABY_REUSABLE_DECOR_EFFECT_ORBIT_DOTS:
        case TABY_REUSABLE_DECOR_EFFECT_SPIRAL_SPARKS:
        case TABY_REUSABLE_DECOR_EFFECT_CHECKER_BURST:
            return TABY_REUSABLE_DECOR_EFFECT_FIREWORK_DOTS;
        case TABY_REUSABLE_DECOR_EFFECT_HEART_BURST:
        case TABY_REUSABLE_DECOR_EFFECT_PETAL_SWIRL:
            return TABY_REUSABLE_DECOR_EFFECT_HEART_BURST;
        case TABY_REUSABLE_DECOR_EFFECT_HEART_RAIN:
            return TABY_REUSABLE_DECOR_EFFECT_HEART_RAIN;
        case TABY_REUSABLE_DECOR_EFFECT_GOLD_RINGS:
        case TABY_REUSABLE_DECOR_EFFECT_BUBBLE_POP:
        case TABY_REUSABLE_DECOR_EFFECT_NEON_ARC:
            return TABY_REUSABLE_DECOR_EFFECT_GOLD_RINGS;
        case TABY_REUSABLE_DECOR_EFFECT_RIBBON_SWEEP:
            return TABY_REUSABLE_DECOR_EFFECT_RIBBON_SWEEP;
        case TABY_REUSABLE_DECOR_EFFECT_GLOW_WAVE:
        case TABY_REUSABLE_DECOR_EFFECT_PRISM_SHARDS:
        case TABY_REUSABLE_DECOR_EFFECT_AURORA_SWEEP:
            return TABY_REUSABLE_DECOR_EFFECT_AURORA_SWEEP;
        default: return TABY_REUSABLE_DECOR_EFFECT_NONE;
    }
}

static void decor_points(c6_ui_decor_shape_t *shape, const c6_ui_decor_point_t *points,
                          unsigned count, int scale) {
    shape->count = (uint8_t)count;
    for (unsigned j = 0; j < count; ++j) {
        shape->points[j].x = points[j].x * scale / 16;
        shape->points[j].y = points[j].y * scale / 16;
    }
}

bool c6_ui_model_decor_frame(const c6_ui_model_t *m, int64_t now,
                             c6_ui_decor_frame_t *frame) {
    if (!frame) return false;
    frame->count = 0;
    if (!m || !m->active ||
        (m->card.kind != TABY_REUSABLE_CARD_HEADLINE && m->card.kind != TABY_REUSABLE_CARD_TEXT) ||
        !geometry_valid(m->width, m->height)) return false;
    taby_reusable_decor_effect_t family = c6_ui_model_decor_family(m->card.behavior.decor_effect);
    uint64_t age = elapsed(m, now);
    if (family == TABY_REUSABLE_DECOR_EFFECT_NONE ||
        age >= (uint64_t)m->card.behavior.decor_effect_seconds * 1000000U) return false;

    /* A 1.6-second cycle is evaluated from the card epoch, never accumulated.
       All outlines are <=54 pixels, with stroke clearance inside the LCD. */
    static const uint32_t colors[] = {0x79CFFF,0xFFE09A,0xB8A3FF,0x78E5CE};
    static const int8_t dx[] = {16,11,0,-11,-16,-11,0,11};
    static const int8_t dy[] = {0,11,16,11,0,-11,-16,-11};
    static const c6_ui_decor_point_t heart[] = {
        {8,4},{5,1},{2,1},{0,4},{0,7},{3,11},{8,16},{13,11},{16,7},{16,4},{14,1},{11,1},{8,4}
    };
    static const c6_ui_decor_point_t star[] = {
        {8,0},{10,6},{16,8},{10,10},{8,16},{6,10},{0,8},{6,6},{8,0},{0,16}
    };
    static const c6_ui_decor_point_t comet[] = {{0,0},{22,10},{29,14},{22,14},{25,10},{29,14}};
    static const c6_ui_decor_point_t spark[] = {{3,0},{6,3},{3,6},{0,3},{3,0}};
    static const c6_ui_decor_point_t ribbon[] = {{0,16},{9,6},{18,10},{27,2},{36,6},{45,0},{52,4}};
    static const c6_ui_decor_point_t aurora[] = {{0,18},{6,8},{13,2},{20,0},{27,5},{34,15},{41,21},{48,23},{54,18}};
    static const c6_ui_decor_point_t circle[] = {
        {16,0},{22,1},{27,5},{31,10},{32,16},{31,22},{27,27},{22,31},
        {16,32},{10,31},{5,27},{1,22},{0,16},{1,10},{5,5},{10,1},{16,0}
    };
    unsigned t = (unsigned)((age % 1600000U) / 1000U);
    frame->count = C6_UI_DECOR_OBJECTS;
    for (unsigned i = 0; i < frame->count; ++i) {
        c6_ui_decor_shape_t *p = &frame->shapes[i];
        memset(p, 0, sizeof(*p));
        unsigned phase = (t + i*173U) % 1600U;
        p->color = colors[i%4]; p->width = 2; p->opacity = 175;
        int travel = (int)phase * 42 / 1600;
        int x = (int)(i%4)*(m->width-64)/3 + 16;
        int y = i<4 ? 24 : m->height-72;
        switch (family) {
            case TABY_REUSABLE_DECOR_EFFECT_SHOOTING_STARS:
                decor_points(p, star, sizeof(star)/sizeof(*star), 13);
                x = 10 + (int)((phase*(unsigned)(m->width-40))/1600U);
                y += travel/2;
                break;
            case TABY_REUSABLE_DECOR_EFFECT_COMET_BURST:
                decor_points(p, comet, sizeof(comet)/sizeof(*comet), 16);
                x += dx[i]*travel/32; y += dy[i]*travel/32;
                break;
            case TABY_REUSABLE_DECOR_EFFECT_FIREWORK_DOTS:
                decor_points(p, spark, sizeof(spark)/sizeof(*spark), 16);
                /* Two radial bursts of four sparks, above and below text. */
                x = m->width/2 + dx[i]*travel/16;
                y = (i<4 ? 44 : m->height-44) + dy[i]*travel/16;
                p->opacity = (uint8_t)(240-phase/10);
                break;
            case TABY_REUSABLE_DECOR_EFFECT_HEART_BURST:
                decor_points(p, heart, sizeof(heart)/sizeof(*heart), 14);
                x = m->width/2-7 + dx[i]*travel/16;
                y += dy[i]*travel/24;
                p->color = i%2 ? 0xFF9BBF : 0xF06D9D;
                break;
            case TABY_REUSABLE_DECOR_EFFECT_HEART_RAIN:
                decor_points(p, heart, sizeof(heart)/sizeof(*heart), 12);
                y = 10 + (int)(phase*(unsigned)(m->height-36)/1600U);
                p->color = i%2 ? 0xFF9BBF : 0xF06D9D;
                break;
            case TABY_REUSABLE_DECOR_EFFECT_GOLD_RINGS:
                decor_points(p, circle, sizeof(circle)/sizeof(*circle), 6+(int)phase/100);
                p->color = i%2 ? 0xFFE5A6 : 0xE8B958;
                break;
            case TABY_REUSABLE_DECOR_EFFECT_RIBBON_SWEEP:
                decor_points(p, ribbon, sizeof(ribbon)/sizeof(*ribbon), 16);
                x = 8+(int)(phase*(unsigned)(m->width-72)/1600U);
                y += (int)(i%4)*5; p->width=3;
                break;
            case TABY_REUSABLE_DECOR_EFFECT_AURORA_SWEEP:
                decor_points(p, aurora, sizeof(aurora)/sizeof(*aurora), 16);
                x = 8+(int)(phase*(unsigned)(m->width-74)/1600U);
                y += (int)(i%4)*5; p->width=4; p->opacity=115;
                p->color = i%2 ? 0x75E8BE : 0x9C94F2;
                break;
            default: return false;
        }
        int max_x=0, max_y=0;
        for (unsigned j=0; j<p->count; ++j) {
            if (p->points[j].x>max_x) max_x=p->points[j].x;
            if (p->points[j].y>max_y) max_y=p->points[j].y;
        }
        p->x=clamp(x,6,m->width-max_x-7);
        p->y=clamp(y,6,m->height-max_y-7);
    }
    return true;
}
