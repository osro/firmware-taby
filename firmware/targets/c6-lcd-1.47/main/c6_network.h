#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool wifi_started,wifi_connected,ap_active,ble_started,ble_advertising,ble_connected;
    char ip[16],station_ssid[33],ap_ssid[33],ap_password[17],last_error[64];
} c6_network_status_t;
esp_err_t c6_network_init(const char *device_id);
esp_err_t c6_network_activate(uint8_t mode);
bool c6_network_command(const char *,char *,size_t);
void c6_network_get_status(c6_network_status_t *);
/* Internal BLE integration; callbacks never call the display owner directly. */
esp_err_t c6_ble_start(const char *name,const char *device_id);
void c6_network_ble_status(bool started,bool advertising,bool connected,const char *error);
