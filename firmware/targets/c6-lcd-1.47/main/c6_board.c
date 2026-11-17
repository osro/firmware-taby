/* Pinout and initialization based on Waveshare's C6 Touch LCD 1.47 demo.
   See ../NOTICE for the upstream source and preserved driver license. */
#include "c6_board.h"
#include "c6_geometry.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
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
static i2c_master_dev_handle_t imu;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t calibration;
static c6_board_status_t state;
static bool fault;
static uint8_t *scanlines;
static uint16_t candidate;
static int64_t candidate_since,next_sensor,next_battery;

int c6_board_width(void) { return state.rotation==90 || state.rotation==270?320:172; }
int c6_board_height(void) { return state.rotation==90 || state.rotation==270?172:320; }
bool c6_board_healthy(void) { return !fault; }
const c6_board_status_t *c6_board_status(void) { return &state; }

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
    return c6_board_draw_rect(0,y,c6_board_width(),height,pixels);
}

esp_err_t c6_board_draw_rect(int x,int y,int width,int height,const uint8_t *pixels) {
    if (fault) return ESP_ERR_INVALID_STATE;
    if (x<0 || y<0 || x>=c6_board_width() || y>=c6_board_height() || width<=0 || height<=0 ||
        width>c6_board_width()-x || height>c6_board_height()-y || !pixels) return ESP_ERR_INVALID_ARG;
    for(int row=0;row<height;row+=16) {
        int rows=height-row;if(rows>16)rows=16;
        /* The LCD never owns the caller's buffer, including on a timeout.
           Staging is quarantined by fault until a full device reset. */
        memmove(scanlines,pixels+(size_t)row*width*2,(size_t)width*rows*2);
        while(xSemaphoreTake(transfer_done,0)==pdTRUE){}
        esp_err_t err=esp_lcd_panel_draw_bitmap(panel,x,y+row,x+width,y+row+rows,scanlines);
        if(err==ESP_OK && xSemaphoreTake(transfer_done,pdMS_TO_TICKS(1000))!=pdTRUE)err=ESP_ERR_TIMEOUT;
        if(err!=ESP_OK){fault=true;return err;}
    }
    return ESP_OK;
}

static esp_err_t clear_screen(void) {
    memset(scanlines,0,320*16*2);
    for (int y=0;y<c6_board_height();y+=16) {
        int height=c6_board_height()-y;if(height>16)height=16;
        esp_err_t err=c6_board_draw_rect(0,y,c6_board_width(),height,scanlines);
        if(err!=ESP_OK)return err;
    }
    return ESP_OK;
}

esp_err_t c6_board_set_rotation(uint16_t rotation) {
    if(rotation!=0 && rotation!=90 && rotation!=180 && rotation!=270)return ESP_ERR_INVALID_ARG;
    if(fault)return ESP_ERR_INVALID_STATE;
    if(rotation==state.rotation)return ESP_OK;
    bool swap=rotation==90 || rotation==270;
    esp_err_t err=esp_lcd_panel_swap_xy(panel,swap);
    if(err==ESP_OK)err=esp_lcd_panel_mirror(panel,rotation==90 || rotation==180,rotation==180 || rotation==270);
    if(err==ESP_OK)err=esp_lcd_panel_set_gap(panel,swap?0:34,swap?34:0);
    if(err!=ESP_OK){fault=true;return err;}
    state.rotation=rotation;
    return clear_screen();
}

esp_err_t c6_board_draw_animation(const uint8_t *pixels) {
    if(fault)return ESP_ERR_INVALID_STATE;
    if(c6_board_width()==172)return c6_board_draw_rect(0,20,172,280,pixels);
    const int scaled_width=106, left=(320-scaled_width)/2;
    for(int y=0;y<172;y+=8) {
        int rows=172-y;if(rows>8)rows=8;
        memset(scanlines,0,320*rows*2);
        for(int row=0;row<rows;++row)for(int x=0;x<scaled_width;++x) {
            size_t from=((size_t)((y+row)*280/172)*172+x*172/scaled_width)*2;
            size_t to=((size_t)row*320+left+x)*2;
            scanlines[to]=pixels[from];scanlines[to+1]=pixels[from+1];
        }
        esp_err_t err=c6_board_draw_rect(0,y,320,rows,scanlines);
        if(err!=ESP_OK)return err;
    }
    return ESP_OK;
}

esp_err_t c6_board_bus_init(void) {
    /* Keep the SD card deselected while sharing its SPI bus with the display. */
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_4, 1);
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_14, 1);
    spi_bus_config_t bus = JD9853_PANEL_BUS_SPI_CONFIG(1, 2, C6_LCD_WIDTH*C6_FRAME_HEIGHT*2);
    bus.miso_io_num = 3;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), "c6", "SPI init");
    return ESP_OK;
}

esp_err_t c6_board_init(void) {
    transfer_done = xSemaphoreCreateBinary();
    if (!transfer_done) return ESP_ERR_NO_MEM;
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
    scanlines=heap_caps_calloc(320*16,2,MALLOC_CAP_DMA);
    if(!scanlines)return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(clear_screen(),"c6","LCD clear");
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
    if(i2c_master_probe(i2c,0x6b,20)==ESP_OK) {
        i2c_device_config_t ic={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x6b,.scl_speed_hz=400000};
        if(i2c_master_bus_add_device(i2c,&ic,&imu)==ESP_OK) {
            uint8_t reg=0,id=0;
            if(i2c_master_transmit_receive(imu,&reg,1,&id,1,20)==ESP_OK && id==5) {
                const uint8_t reset[]={0x60,0xb0};
                bool ok=i2c_master_transmit(imu,reset,2,20)==ESP_OK;
                vTaskDelay(pdMS_TO_TICKS(20));
                /* Configure while disabled; CTRL2 aST (bit 7) must stay clear.
                   ±4g / 250Hz matches the 8192 LSB/g conversion in poll. */
                const uint8_t setup[][2]={{0x08,0x00},{0x02,0x40},{0x03,0x15},{0x08,0x01}};
                for(unsigned i=0;ok && i<sizeof(setup)/sizeof(setup[0]);++i)
                    ok=i2c_master_transmit(imu,setup[i],2,20)==ESP_OK;
                /* Cache actual register reads, not the requested setup values. */
                const uint8_t registers[]={0x02,0x03,0x08};
                uint8_t *readback[]={&state.imu_ctrl1,&state.imu_ctrl2,&state.imu_ctrl7};
                for(unsigned i=0;i<sizeof(registers)/sizeof(registers[0]);++i)
                    if(i2c_master_transmit_receive(imu,&registers[i],1,readback[i],1,20)!=ESP_OK)ok=false;
                state.imu_available=ok && state.imu_ctrl1==0x40 &&
                    state.imu_ctrl2==0x15 && state.imu_ctrl7==0x01;
            }
        }
    }
    adc_oneshot_unit_init_cfg_t ac={.unit_id=ADC_UNIT_1};
    if(adc_oneshot_new_unit(&ac,&adc)==ESP_OK) {
        adc_oneshot_chan_cfg_t channel={.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12};
        if(adc_oneshot_config_channel(adc,ADC_CHANNEL_0,&channel)!=ESP_OK)adc=NULL;
        else {
            adc_cali_curve_fitting_config_t cc={.unit_id=ADC_UNIT_1,.chan=ADC_CHANNEL_0,
                .atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12};
            state.battery_calibrated=adc_cali_create_scheme_curve_fitting(&cc,&calibration)==ESP_OK;
        }
    }
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
        if(!c6_touch_rotate(rawx,rawy,state.rotation,x,y))return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

bool c6_board_poll(int64_t now,bool auto_rotate) {
    bool changed=false;
    if(state.imu_available && now>=next_sensor) {
        next_sensor=now+100000;
        uint8_t reg=0x35,data[6];
        if(i2c_master_transmit_receive(imu,&reg,1,data,6,20)==ESP_OK) {
            int16_t raw[3];
            for(unsigned i=0;i<3;++i)raw[i]=(int16_t)(data[2*i]|((uint16_t)data[2*i+1]<<8));
            state.x_mg=(int32_t)raw[0]*1000/8192;
            state.y_mg=(int32_t)raw[1]*1000/8192;
            state.z_mg=(int32_t)raw[2]*1000/8192;
            ++state.imu_samples;
            uint16_t next=c6_gravity_rotation(state.x_mg,state.y_mg,state.z_mg,state.rotation);
            if(next!=candidate){candidate=next;candidate_since=now;}
            if(auto_rotate && next!=state.rotation && now-candidate_since>=1200000)
                changed=c6_board_set_rotation(next)==ESP_OK;
        } else ++state.imu_errors;
    }
    if(adc && now>=next_battery) {
        next_battery=now+1000000;
        int raw=0,mv=0;
        if(adc_oneshot_read(adc,ADC_CHANNEL_0,&raw)==ESP_OK && state.battery_calibrated &&
            adc_cali_raw_to_voltage(calibration,raw,&mv)==ESP_OK)state.battery_mv=mv*3;
    }
    return changed;
}
