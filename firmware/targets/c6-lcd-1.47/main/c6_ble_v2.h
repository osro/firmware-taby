#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Portable wire builders. Failure clears out[0] when a buffer is supplied.
 * Identity fields reject JSON metacharacters/control bytes rather than escape
 * or truncate identity. Neither builder changes the owner's signal counter. */
bool c6_ble_v2_identity(char *out, size_t capacity, const char *device_id,
                        const char *firmware_version);
bool c6_ble_v2_choice(char *out, size_t capacity, uint32_t signal,
                      const char *selection);
bool c6_ble_v2_orientation(char *out, size_t capacity, uint16_t degrees,
                           const char *mode, const char *orientation,
                           bool auto_supported);
