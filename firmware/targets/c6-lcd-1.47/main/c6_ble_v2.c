#include "c6_ble_v2.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool begin(char *out, size_t capacity) {
    if (!out || !capacity) return false;
    out[0] = 0;
    return true;
}
static bool complete(char *out, size_t capacity, int length) {
    if (length < 0 || (size_t)length >= capacity) {
        out[0] = 0;
        return false;
    }
    return true;
}
static bool identity_field(const char *text, size_t maximum, bool device_id) {
    if (!text || !text[0]) return false;
    for (size_t i = 0; text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (i >= maximum) return false;
        if (device_id) {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-')) return false;
        } else if (c < 32 || c > 126 || c == '"' || c == '\\') return false;
    }
    return true;
}
bool c6_ble_v2_identity(char *out, size_t capacity, const char *device_id,
                        const char *firmware_version) {
    if (!begin(out, capacity)) return false;
    if (!identity_field(device_id, 120, true) ||
        !identity_field(firmware_version, 80, false)) return false;
    return complete(out, capacity, snprintf(out, capacity,
        "{\"protocol\":2,\"deviceId\":\"%s\",\"firmwareVersion\":\"%s\"}",
        device_id, firmware_version));
}
bool c6_ble_v2_choice(char *out, size_t capacity, uint32_t signal,
                      const char *selection) {
    if (!begin(out, capacity) || !selection) return false;
    const char *choice = !strcmp(selection, "choice_1") ? "left" :
                         !strcmp(selection, "choice_2") ? "right" : "none";
    return complete(out, capacity, snprintf(out, capacity,
        "{\"signal\":%" PRIu32 ",\"choice\":\"%s\"}", signal, choice));
}
static const char *orientation_name(uint16_t degrees) {
    switch (degrees) {
        case 0: return "left";
        case 90: return "90";
        case 180: return "right";
        case 270: return "270";
        default: return NULL;
    }
}
static bool valid_mode(const char *mode) {
    return mode && (!strcmp(mode, "left") || !strcmp(mode, "right") ||
                    !strcmp(mode, "90") || !strcmp(mode, "270") || !strcmp(mode, "auto"));
}
bool c6_ble_v2_orientation(char *out, size_t capacity, uint16_t degrees,
                           const char *mode, const char *orientation,
                           bool auto_supported) {
    if (!begin(out, capacity)) return false;
    const char *expected = orientation_name(degrees);
    if (!expected || !valid_mode(mode) || !orientation || strcmp(expected, orientation) ||
        (strcmp(mode, "auto") && strcmp(mode, orientation))) return false;
    return complete(out, capacity, snprintf(out, capacity,
        "OK DISPLAY_ORIENTATION %u %s %s %u rectangle",
        (unsigned)degrees, mode, orientation, auto_supported ? 1u : 0u));
}
