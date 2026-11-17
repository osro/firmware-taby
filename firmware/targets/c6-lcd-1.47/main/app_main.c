#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "c6_board.h"
#include "c6_codec.h"
#include "generated/c6_catalog.h"
#include "driver/usb_serial_jtag.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "taby_animation_assets.h"
#include "taby_line_reader.h"
#include "taby_state_machine.h"
#include "taby_transport_protocol.h"

static const uint8_t *assets;
static uint8_t *pixels;
static const c6_clip_t *clip, *sequence_next;
static size_t cursor;
static uint16_t frame;
static int64_t due;
static uint32_t rendered, cycles, decode_errors, display_errors;
static bool display_fault;
static taby_state_machine_t machine;
static uint32_t touches, gesture_signal, touch_samples, touch_errors;
static uint16_t touch_x, touch_y;
static bool finger_down, holding;
static int64_t finger_since;
static const char *gesture = "none";
static unsigned brightness = 70;
static char device_id[40];

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1]<<8); }
static uint32_t u32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void reply(const char *text) {
    usb_serial_jtag_write_bytes(text, strlen(text), pdMS_TO_TICKS(100));
    usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(100));
}
static const c6_clip_t *find_clip(const char *id) {
    taby_animation_asset_t canonical;
    if (!id) return NULL;
    if (taby_animation_asset_for_id(id, &canonical)) id = canonical.animation_id;
    for (size_t i=0; i<C6_CLIP_COUNT; ++i) {
        if (strcmp(id, c6_clips[i].id) == 0) return &c6_clips[i];
    }
    return NULL;
}
static bool select_clip(const c6_clip_t *selected) {
    if (display_fault || !selected || selected->offset > C6_ASSET_BYTES ||
        selected->size > C6_ASSET_BYTES-selected->offset || selected->size < 12) return false;
    const uint8_t *header = assets+selected->offset;
    if (memcmp(header, "TC6A", 4) || u16(header+4) != C6_LCD_WIDTH ||
        u16(header+6) != C6_FRAME_HEIGHT || u16(header+8) == 0 || u16(header+10) > 1) return false;
    clip = selected;
    cursor = 12;
    frame = 0;
    due = 0;
    return true;
}
static bool available(const char *id) { return find_clip(id) != NULL; }
static const char *state_name(void) { return taby_transport_state_name(machine.current_state); }
static bool clear(void) {
    if (!select_clip(find_clip("idle_01_loop"))) return false;
    sequence_next = NULL;
    machine.current_state = TABY_STATE_AMBIENT_IDLE;
    return true;
}
static bool apply(const taby_transport_resolution_t *resolution) {
    const char *required[2];
    size_t count = taby_transport_required_animations(resolution, required);
    /* UI/timer states have no equivalent in this deliberately small port. */
    if (!count || !select_clip(find_clip(required[0]))) return false;
    sequence_next = count > 1 ? find_clip(required[1]) : NULL;
    taby_state_machine_apply_command(&machine, resolution->command);
    return true;
}
static const taby_transport_display_hooks_t hooks = {
    .animation_available=available, .apply=apply, .state_name=state_name, .clear=clear,
};

static void playback(int64_t now) {
    if (!clip || now < due) return;
    const uint8_t *start = assets+clip->offset;
    if (frame >= u16(start+8)) {
        ++cycles;
        if (sequence_next) {
            const c6_clip_t *next = sequence_next;
            sequence_next = NULL;
            select_clip(next);
        } else if (u16(start+10)) {
            cursor = 12;
            frame = 0;
        } else {
            clear();
        }
        start = assets+clip->offset;
    }
    if (cursor > clip->size || clip->size-cursor < 6) goto corrupt;
    const uint8_t *header = start+cursor;
    uint16_t duration = u16(header);
    uint32_t length = u32(header+2);
    cursor += 6;
    if (!duration || length < 128 || length > clip->size-cursor) goto corrupt;
    const uint8_t *data = start+cursor;
    if (!c6_rle_decode(data+128, length-128, data, pixels, C6_LCD_WIDTH*C6_FRAME_HEIGHT)) goto corrupt;
    if (c6_board_draw((C6_LCD_HEIGHT-C6_FRAME_HEIGHT)/2, C6_FRAME_HEIGHT, pixels) != ESP_OK) {
        ++display_errors;
        /* Do not reuse a buffer while a timed-out DMA transfer could still own it. */
        display_fault = true; /* A reset is required before accepting another clip. */
        clip = NULL;
        return;
    }
    ++rendered;
    ++frame;
    cursor += length;
    due = now + (int64_t)duration*1000;
    return;
corrupt:
    ++decode_errors;
    clip = NULL;  /* Keep USB diagnostics alive; don't crash or loop over bad data. */
}

static void touch_poll(int64_t now) {
    bool pressed;
    esp_err_t err = c6_board_touch(&pressed, &touch_x, &touch_y);
    if (err != ESP_OK) { ++touch_errors; return; }
    ++touch_samples;
    if (pressed && !finger_down) {
        finger_since = now;
        holding = false;
    }
    if (pressed && !holding && now-finger_since >= 700000) {
        holding = true;
        gesture = "hold_start";
        ++gesture_signal;
    }
    if (!pressed && finger_down) {
        if (holding) {
            gesture = "hold_end";
            ++gesture_signal;
        } else if (now-finger_since >= 60000) {
            ++touches;
            gesture = "tap";
            ++gesture_signal;
        }
    }
    finger_down = pressed;
}

static void handle_command(const char *line) {
    char response[1400];
    if (strcmp(line, "PING") == 0) {
        reply("TABY:PONG");
    } else if (strcmp(line, "INFO") == 0) {
        snprintf(response, sizeof(response),
            "TABY:INFO {\"firmware_version\":\"0.1.0-c6-dev.1\","
            "\"assets_version\":\"0.1.0-dev\",\"hardware_target\":\"c6-lcd-1.47\","
            "\"display_shape\":\"rectangle\",\"display_width\":172,\"display_height\":320,"
            "\"device_id\":\"%s\",\"identity_source\":\"development_chip_mac\","
            "\"preferred_transport\":\"usb\",\"transport_onboarding_complete\":true,"
            "\"usb_bridge_ready\":true,\"development_build\":true,"
            "\"capabilities\":[\"info\",\"diag\",\"touch\",\"gesture\",\"clear\","
            "\"brightness\",\"unsupported_animation\"],\"animation_count\":%u,"
            "\"assets_sha256\":\"%s\"}", device_id, (unsigned)C6_CLIP_COUNT, C6_ASSET_SHA256);
        reply(response);
    } else if (strcmp(line, "DIAG") == 0) {
        snprintf(response, sizeof(response),
            "TABY:DIAG {\"animation\":\"%s\",\"state\":\"%s\",\"frame\":%u,"
            "\"rendered_frames\":%" PRIu32 ",\"completed_cycles\":%" PRIu32 ","
            "\"decode_errors\":%" PRIu32 ",\"display_errors\":%" PRIu32 ","
            "\"free_heap\":%u,\"minimum_free_heap\":%u,\"largest_free_block\":%u,"
            "\"touch_samples\":%" PRIu32 ",\"touch_errors\":%" PRIu32 ","
            "\"touch_count\":%" PRIu32 ",\"touch_pressed\":%s,\"touch_x\":%u,\"touch_y\":%u}",
            clip ? clip->id : "", state_name(), frame, rendered, cycles, decode_errors, display_errors,
            (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), touch_samples,
            touch_errors, touches, finger_down ? "true" : "false", touch_x, touch_y);
        reply(response);
    } else if (strcmp(line, "ANIMATIONS") == 0) {
        reply("TABY:ANIMATIONS_BEGIN");
        for (size_t i=0; i<C6_CLIP_COUNT; ++i) {
            snprintf(response, sizeof(response), "TABY:ANIMATION %s", c6_clips[i].id);
            reply(response);
        }
        reply("TABY:ANIMATIONS_END");
    } else if (strcmp(line, "TOUCH_SIGNAL") == 0) {
        snprintf(response, sizeof(response), "TABY:TOUCH_SIGNAL %" PRIu32, touches);
        reply(response);
    } else if (strcmp(line, "GESTURE_SIGNAL") == 0) {
        snprintf(response, sizeof(response), "TABY:GESTURE_SIGNAL {\"signal\":%" PRIu32 ",\"gesture\":\"%s\"}",
                 gesture_signal, gesture);
        reply(response);
    } else if (strcmp(line, "CHOICE_SIGNAL") == 0) {
        reply("TABY:ERR unsupported_command CHOICE_SIGNAL");
    } else if (!strncmp(line, "TRANSPORT_MODE ", 15) || !strncmp(line, "TRANSPORT_DEFAULT ", 18)) {
        const char *mode = strchr(line, ' ')+1;
        if (strcmp(mode, "usb")) reply("TABY:ERR unsupported_transport");
        else reply(!strncmp(line, "TRANSPORT_DEFAULT ", 18) ?
                   "TABY:OK TRANSPORT_DEFAULT" : "TABY:OK TRANSPORT_MODE");
    } else {
        const char *command = !strncmp(line, "CMD ", 4) ? line+4 : line;
        if (!strcmp(command, "BRIGHTNESS?")) {
            snprintf(response, sizeof(response), "TABY:BRIGHTNESS {\"percent\":%u}", brightness);
        } else if (!strncmp(command, "BRIGHTNESS ", 11)) {
            unsigned value; char tail;
            if (sscanf(command+11, "%u%c", &value, &tail) != 1 || value > 100) {
                reply("TABY:ERR invalid_brightness"); return;
            }
            brightness = value;
            c6_board_brightness(value);
            snprintf(response, sizeof(response), "TABY:BRIGHTNESS {\"percent\":%u}", brightness);
        } else {
            taby_transport_handle_display_command(command, &hooks, "TABY:", true, response, sizeof(response));
        }
        reply(response);
    }
}

void app_main(void) {
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(device_id, sizeof(device_id), "taby-c6-%02x%02x%02x%02x%02x%02x",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    ESP_ERROR_CHECK(c6_board_init());
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "assets");
    ESP_ERROR_CHECK(partition && C6_ASSET_BYTES <= partition->size ? ESP_OK : ESP_ERR_NOT_FOUND);
    esp_partition_mmap_handle_t mapping;
    ESP_ERROR_CHECK(esp_partition_mmap(partition, 0, C6_ASSET_BYTES, ESP_PARTITION_MMAP_DATA,
                                     (const void **)&assets, &mapping));
    ESP_ERROR_CHECK(esp_crc32_le(0, assets, C6_ASSET_BYTES) == C6_ASSET_CRC32 ? ESP_OK : ESP_ERR_INVALID_CRC);
    pixels = heap_caps_malloc(C6_LCD_WIDTH*C6_FRAME_HEIGHT*2, MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    ESP_ERROR_CHECK(pixels ? ESP_OK : ESP_ERR_NO_MEM);
    taby_state_machine_init(&machine);
    ESP_ERROR_CHECK(select_clip(find_clip("startup")) ? ESP_OK : ESP_ERR_INVALID_SIZE);
    machine.current_state = TABY_STATE_AMBIENT_STARTUP;
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size=4096, .rx_buffer_size=4096};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    char line[1024];
    taby_line_reader_t reader;
    taby_line_reader_init(&reader, line, sizeof(line));
    int64_t next_touch = 0;
    while (true) {
        uint8_t incoming[128];
        int count = usb_serial_jtag_read_bytes(incoming, sizeof(incoming), pdMS_TO_TICKS(2));
        for (int i=0; i<count; ++i) {
            taby_line_reader_event_t event = taby_line_reader_push(&reader, incoming[i]);
            if (event == TABY_LINE_READER_LINE) handle_command(reader.buffer);
            else if (event == TABY_LINE_READER_TOO_LONG) reply("TABY:ERR line_too_long");
        }
        int64_t now = esp_timer_get_time();
        playback(now);
        if (now >= next_touch) { touch_poll(now); next_touch = now+20000; }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
