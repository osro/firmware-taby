#pragma once

#include <stdbool.h>
#include <stdint.h>

/* One owner: invoke all entry points on the runtime main thread. */
void c6_ui_init(void);
void c6_ui_tick(int64_t now_us);
void c6_ui_hide(void);
bool c6_ui_active(void);
void c6_ui_touch(bool pressed, uint16_t x, uint16_t y);
const char *c6_ui_state_name(void);
void c6_ui_refresh_orientation(void);
