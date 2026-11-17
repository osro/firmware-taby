#include "c6_preferences.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "taby_eye_motion.h"

void c6_preferences_defaults(c6_preferences_t *preferences) {
    if (preferences) {
        *preferences = (c6_preferences_t){
            .brightness_percent = 70,
            .orientation = 0,
            .eye_motion = TABY_EYE_MOTION_DEFAULT,
            .preferred_transport = 0,
        };
    }
}

bool c6_preferences_valid(const c6_preferences_t *preferences) {
    if (!preferences || preferences->brightness_percent > 100 ||
        preferences->preferred_transport > 2 ||
        !taby_eye_motion_from_stored(preferences->eye_motion, NULL)) {
        return false;
    }
    switch (preferences->orientation) {
        case 0: case 90: case 180: case 270: case 360:
            return true;
        default:
            return false;
    }
}

/* Extract one bounded value; reject any non-whitespace trailing command data. */
static bool command_value(const char *text, char *value, size_t size) {
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    size_t length = 0;
    while (text[length] && !isspace((unsigned char)text[length])) {
        if (length + 1 >= size) {
            return false;
        }
        value[length] = text[length];
        ++length;
    }
    if (!length) {
        return false;
    }
    value[length] = '\0';
    text += length;
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    return *text == '\0';
}

static bool orientation_value(char *value, uint16_t *orientation) {
    for (char *p = value; *p; ++p) {
        *p = (char)tolower((unsigned char)*p);
    }
    static const struct { const char *name; uint16_t value; } modes[] = {
        {"left", 0}, {"0", 0}, {"right", 180}, {"180", 180},
        {"90", 90}, {"rotate_90", 90}, {"270", 270}, {"rotate_270", 270}, {"auto", 360},
    };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        if (!strcmp(value, modes[i].name)) {
            *orientation = modes[i].value;
            return true;
        }
    }
    return false;
}

static const char *orientation_name(uint16_t orientation) {
    switch (orientation) {
        case 90: return "rotate_90";
        case 180: return "right";
        case 270: return "rotate_270";
        case 360: return "auto";
        default: return "left";
    }
}

c6_pref_result_t c6_preferences_command(const c6_preferences_t *current,
                                       const char *command,
                                       c6_preferences_t *next,
                                       char *reply, size_t reply_size) {
    if (reply && reply_size) {
        reply[0] = '\0';
    }
    if (current && next) {
        *next = *current;
    }
    if (!c6_preferences_valid(current) || !command || !next) {
        return C6_PREF_INVALID;
    }

    c6_preferences_t candidate = *current;
    c6_pref_result_t result = C6_PREF_QUERY;
    enum { NONE, BRIGHTNESS, ORIENTATION, EYES, TRANSPORT } kind = NONE;
    const char *error = NULL;
    char value[32];
    taby_eye_motion_t eye = TABY_EYE_MOTION_DEFAULT;
    taby_eye_motion_command_t eye_command = taby_eye_motion_parse_command(command, &eye);

    if (!strcmp(command, "BRIGHTNESS") || !strcmp(command, "BRIGHTNESS?")) {
        kind = BRIGHTNESS;
    } else if (!strncmp(command, "BRIGHTNESS ", 11)) {
        kind = BRIGHTNESS;
        char *end = NULL;
        errno = 0;
        bool token_ok = command_value(command + 11, value, sizeof(value));
        long percent = token_ok ? strtol(value, &end, 10) : -1;
        if (!token_ok || errno == ERANGE || end == value || *end || percent < 0 || percent > 100) {
            error = "brightness_invalid_value";
        } else {
            candidate.brightness_percent = (uint8_t)percent;
            result = C6_PREF_CHANGED;
        }
    } else if (!strcmp(command, "DISPLAY_ORIENTATION") || !strcmp(command, "DISPLAY_ORIENTATION?")) {
        kind = ORIENTATION;
    } else if (!strncmp(command, "DISPLAY_ORIENTATION ", 20)) {
        kind = ORIENTATION;
        if (!command_value(command + 20, value, sizeof(value)) ||
            !orientation_value(value, &candidate.orientation)) {
            error = "display_orientation_invalid_value";
        } else {
            result = C6_PREF_CHANGED;
        }
    } else if (eye_command != TABY_EYE_MOTION_COMMAND_NONE) {
        kind = EYES;
        if (eye_command == TABY_EYE_MOTION_COMMAND_INVALID) {
            error = "eye_motion_invalid_value";
        } else if (eye_command == TABY_EYE_MOTION_COMMAND_SET) {
            candidate.eye_motion = (uint8_t)eye;
            result = C6_PREF_CHANGED;
        }
    } else if (!strncmp(command, "TRANSPORT_DEFAULT ", 18)) {
        kind = TRANSPORT;
        if (!command_value(command + 18, value, sizeof(value))) {
            error = "invalid_transport_mode";
        } else if (!strcmp(value, "usb")) {
            candidate.preferred_transport = 0;
        } else if (!strcmp(value, "wifi")) {
            candidate.preferred_transport = 1;
        } else if (!strcmp(value, "ble") || !strcmp(value, "bluetooth")) {
            candidate.preferred_transport = 2;
        } else {
            error = "invalid_transport_mode";
        }
        result = C6_PREF_CHANGED;
    }
    if (kind == NONE) {
        return C6_PREF_UNHANDLED;
    }

    char response[256];
    int length;
    if (error) {
        result = C6_PREF_INVALID;
        length = snprintf(response, sizeof(response), "TABY:ERR %s", error);
    } else if (kind == BRIGHTNESS) {
        unsigned percent = candidate.brightness_percent;
        length = snprintf(response, sizeof(response),
                          "TABY:BRIGHTNESS {\"percent\":%u,\"raw\":%u}",
                          percent, (percent * 255U + 50U) / 100U);
    } else if (kind == ORIENTATION) {
        uint16_t effective = candidate.orientation == 360 ? 0 : candidate.orientation;
        length = snprintf(response, sizeof(response),
                          "TABY:DISPLAY_ORIENTATION {\"mode\":\"%s\",\"orientation\":\"%s\","
                          "\"rotation_degrees\":%u,\"auto_supported\":true,\"display_shape\":\"rectangle\"}",
                          orientation_name(candidate.orientation), orientation_name(effective),
                          (unsigned)effective);
    } else if (kind == EYES) {
        length = snprintf(response, sizeof(response),
                          "TABY:EYE_MOTION {\"mode\":\"%s\",\"modes\":[\"normal\",\"calm\",\"still\"]}",
                          taby_eye_motion_name((taby_eye_motion_t)candidate.eye_motion));
    } else {
        length = snprintf(response, sizeof(response), "TABY:OK TRANSPORT_DEFAULT");
    }
    if (length < 0 || (size_t)length >= sizeof(response) || !reply || (size_t)length >= reply_size) {
        return C6_PREF_INVALID;
    }
    memcpy(reply, response, (size_t)length + 1);
    if (result == C6_PREF_CHANGED) {
        *next = candidate;
    }
    return result;
}
