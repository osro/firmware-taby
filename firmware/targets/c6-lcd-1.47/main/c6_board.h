#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define C6_LCD_WIDTH 172
#define C6_LCD_HEIGHT 320
#define C6_FRAME_HEIGHT 280

esp_err_t c6_board_init(void);
esp_err_t c6_board_draw(int y, int height, const uint8_t *pixels);
/* ESP_OK means a valid touch sample, including a released finger. */
esp_err_t c6_board_touch(bool *pressed, uint16_t *x, uint16_t *y);
void c6_board_brightness(unsigned percent);
