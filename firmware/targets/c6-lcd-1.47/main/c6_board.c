/* Pinout and initialization based on Waveshare's C6 Touch LCD 1.47 demo.
   See ../NOTICE for the upstream source and preserved driver license. */
#include "c6_board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_jd9853.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t transfer_done;
static i2c_master_dev_handle_t touch;

static bool IRAM_ATTR color_done(esp_lcd_panel_io_handle_t io,
                               esp_lcd_panel_io_event_data_t *event, void *ctx) {
    (void)io; (void)event; (void)ctx;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(transfer_done, &wake);
    return wake == pdTRUE;
}

void c6_board_brightness(unsigned percent) {
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t c6_board_draw(int y, int height, const uint8_t *pixels) {
    if (y < 0 || height <= 0 || y+height > C6_LCD_HEIGHT || !pixels) return ESP_ERR_INVALID_ARG;
    while (xSemaphoreTake(transfer_done, 0) == pdTRUE) {}
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, y, C6_LCD_WIDTH, y+height, pixels);
    if (err != ESP_OK) return err;
    return xSemaphoreTake(transfer_done, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t c6_board_init(void) {
    transfer_done = xSemaphoreCreateBinary();
    if (!transfer_done) return ESP_ERR_NO_MEM;
    /* Keep the SD card deselected while sharing its SPI bus with the display. */
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_4, 1);
    spi_bus_config_t bus = JD9853_PANEL_BUS_SPI_CONFIG(1, 2, C6_LCD_WIDTH*C6_FRAME_HEIGHT*2);
    bus.miso_io_num = 3;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), "c6", "SPI init");
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t cfg = JD9853_PANEL_IO_SPI_CONFIG(14, 15, color_done, NULL);
    cfg.pclk_hz = 40*1000*1000;
    cfg.trans_queue_depth = 1;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &io), "c6", "LCD IO");
    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = 22, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9853(io, &pc, &panel), "c6", "LCD driver");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), "c6", "LCD reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), "c6", "LCD init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), "c6", "LCD inversion");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel, 34, 0), "c6", "LCD offset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel, false, false), "c6", "LCD mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), "c6", "LCD on");
    ledc_timer_config_t timer = {.speed_mode=LEDC_LOW_SPEED_MODE, .timer_num=LEDC_TIMER_0,
        .duty_resolution=LEDC_TIMER_10_BIT, .freq_hz=5000, .clk_cfg=LEDC_AUTO_CLK};
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), "c6", "backlight timer");
    ledc_channel_config_t channel = {.gpio_num=23, .speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_0, .timer_sel=LEDC_TIMER_0, .duty=0};
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), "c6", "backlight");
    uint8_t *black = heap_caps_calloc(C6_LCD_WIDTH*16, 2, MALLOC_CAP_DMA);
    if (!black) return ESP_ERR_NO_MEM;
    for (int y = 0; y < C6_LCD_HEIGHT; y += 16) {
        esp_err_t err = c6_board_draw(y, 16, black);
        if (err != ESP_OK) return err; /* DMA may still own black until reset. */
    }
    free(black);
    c6_board_brightness(70);
    i2c_master_bus_handle_t i2c;
    i2c_master_bus_config_t bc = {.i2c_port=0, .sda_io_num=18, .scl_io_num=19,
        .clk_source=I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt=7,
        .flags.enable_internal_pullup=true};
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bc, &i2c), "c6", "I2C bus");
    gpio_set_direction(GPIO_NUM_20, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_20, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(GPIO_NUM_20, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_direction(GPIO_NUM_21, GPIO_MODE_INPUT);
    i2c_device_config_t dc = {.dev_addr_length=I2C_ADDR_BIT_LEN_7,
        .device_address=0x63, .scl_speed_hz=400000};
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c, &dc, &touch), "c6", "touch device");
    return ESP_OK;
}

esp_err_t c6_board_touch(bool *pressed, uint16_t *x, uint16_t *y) {
    uint8_t reg = 1, data[14];
    /* Vendor controller expects separate write/read transactions (STOP between). */
    esp_err_t err = i2c_master_transmit(touch, &reg, 1, 20);
    if (err != ESP_OK) return err;
    err = i2c_master_receive(touch, data, sizeof(data), 20);
    if (err != ESP_OK) return err;
    *pressed = (data[1] & 15) != 0;
    if (*pressed) {
        unsigned rawx = ((data[2] & 15) << 8) | data[3];
        unsigned rawy = ((data[4] & 15) << 8) | data[5];
        if (rawx >= C6_LCD_WIDTH || rawy >= C6_LCD_HEIGHT) return ESP_ERR_INVALID_RESPONSE;
        *x = C6_LCD_WIDTH-1-rawx;  /* Same portrait orientation as vendor example. */
        *y = rawy;
    }
    return ESP_OK;
}
