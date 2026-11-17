#include "c6_preferences.h"

#include <string.h>

#include "nvs.h"

#define PREFERENCES_NAMESPACE "taby_c6"
#define PREFERENCES_KEY "preferences"
#define BLOB_SIZE 16U
#define BLOB_VERSION 1U

/* Disk format is independent of C padding/enum width and explicitly LE:
 * magic[4], version:u16, length:u16, brightness:u8, orientation:u16,
 * eye_motion:u8, preferred_transport:u8, reserved[3] (zero).
 */
static uint16_t read_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

esp_err_t c6_preferences_load(c6_preferences_t *preferences) {
    if (!preferences) {
        return ESP_ERR_INVALID_ARG;
    }
    c6_preferences_defaults(preferences);
    nvs_handle_t handle;
    esp_err_t error = nvs_open(PREFERENCES_NAMESPACE, NVS_READONLY, &handle);
    if (error != ESP_OK) {
        return error;
    }
    uint8_t blob[BLOB_SIZE];
    size_t size = 0;
    error = nvs_get_blob(handle, PREFERENCES_KEY, NULL, &size);
    if (error == ESP_OK && size != sizeof(blob)) {
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK) {
        error = nvs_get_blob(handle, PREFERENCES_KEY, blob, &size);
    }
    nvs_close(handle);
    if (error != ESP_OK) {
        return error;
    }
    if (size != sizeof(blob) || read_u16(blob + 6) != sizeof(blob)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (memcmp(blob, "TC6P", 4) || read_u16(blob + 4) != BLOB_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    c6_preferences_t candidate = {
        .brightness_percent = blob[8],
        .orientation = read_u16(blob + 9),
        .eye_motion = blob[11],
        .preferred_transport = blob[12],
    };
    if (!c6_preferences_valid(&candidate)) {
        return ESP_ERR_INVALID_ARG;
    }
    *preferences = candidate;
    return ESP_OK;
}

esp_err_t c6_preferences_save(const c6_preferences_t *preferences) {
    if (!c6_preferences_valid(preferences)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t blob[BLOB_SIZE] = {
        'T', 'C', '6', 'P', BLOB_VERSION, 0, BLOB_SIZE, 0,
        preferences->brightness_percent,
        (uint8_t)preferences->orientation, (uint8_t)(preferences->orientation >> 8),
        preferences->eye_motion, preferences->preferred_transport, 0, 0, 0,
    };
    nvs_handle_t handle;
    esp_err_t error = nvs_open(PREFERENCES_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_blob(handle, PREFERENCES_KEY, blob, sizeof(blob));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}
