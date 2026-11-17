#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define C6_LCD_WIDTH 172
#define C6_LCD_HEIGHT 320
#define C6_FRAME_HEIGHT 280

esp_err_t c6_board_init(void);
esp_err_t c6_board_bus_init(void);
esp_err_t c6_board_draw(int y, int height, const uint8_t *pixels);
/* ESP_OK means a valid touch sample, including a released finger. */
esp_err_t c6_board_touch(bool *pressed, uint16_t *x, uint16_t *y);
void c6_board_brightness(unsigned percent);
int c6_board_width(void);
int c6_board_height(void);
bool c6_board_healthy(void);
/* Copies input into private DMA staging; caller may reuse its buffer even if
   the transfer fails. A failure quarantines staging and latches the display. */
esp_err_t c6_board_draw_rect(int x,int y,int width,int height,const uint8_t *pixels);
/* Packed172x280 frames contain sideways art. Presentation normalizes the whole
   frame upright before fitting it to the current logical screen orientation. */
esp_err_t c6_board_draw_animation(const uint8_t *pixels);
esp_err_t c6_board_set_rotation(uint16_t degrees);
typedef struct {
    uint16_t rotation;
    bool imu_available;
    uint8_t imu_ctrl1,imu_ctrl2,imu_ctrl7;
    int16_t x_mg,y_mg,z_mg;
    uint32_t imu_samples,imu_errors;
    int battery_mv;
    bool battery_calibrated;
} c6_board_status_t;
const c6_board_status_t *c6_board_status(void);
bool c6_board_poll(int64_t now_us,bool auto_rotate);
