#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t brightness_percent; /* 0..100 */
    uint16_t orientation;       /* 0, 90, 180, 270; 360 selects auto */
    uint8_t eye_motion;         /* upstream normal=0, calm=1, still=2 */
    uint8_t preferred_transport; /* USB=0, Wi-Fi=1, BLE=2 */
} c6_preferences_t;

typedef enum {
    C6_PREF_UNHANDLED = 0,
    C6_PREF_QUERY = 1,
    C6_PREF_CHANGED = 2,
    C6_PREF_INVALID = 3,
} c6_pref_result_t;

void c6_preferences_defaults(c6_preferences_t *preferences);
bool c6_preferences_valid(const c6_preferences_t *preferences);
/* Canonical desktop wire name for a valid stored orientation or board rotation.
 * Legacy rotate_90/rotate_270 spellings are accepted only as command inputs.
 */
const char *c6_preferences_orientation_name(uint16_t orientation);

/* No I/O: next is current unless a complete, valid mutation can be returned.
 * current and next may alias. A missing/short reply buffer rejects mutation.
 * Brightness raw is the upstream 8-bit mapping, not measured board state.
 * Auto uses neutral (left/0) effective orientation here; the runtime must
 * supply its actual IMU orientation and availability in hardware replies.
 * TRANSPORT_DEFAULT saves intent; activating TRANSPORT_MODE is runtime work.
 */
c6_pref_result_t c6_preferences_command(const c6_preferences_t *current,
                                       const char *command,
                                       c6_preferences_t *next,
                                       char *reply, size_t reply_size);

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* The caller initializes NVS. Neither helper erases or repairs the partition.
 * load installs defaults on every failure (including NOT_FOUND) and returns
 * the storage/validation error; it never writes missing or malformed records.
 * save validates first and commits one versioned, explicitly encoded blob.
 */
esp_err_t c6_preferences_load(c6_preferences_t *preferences);
esp_err_t c6_preferences_save(const c6_preferences_t *preferences);
#endif
