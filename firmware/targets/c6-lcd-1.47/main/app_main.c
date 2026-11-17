#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c6_board.h"
#include "c6_codec.h"
#include "c6_sd.h"
#include "c6_preferences.h"
#include "c6_runtime.h"
#include "c6_ui.h"
#include "c6_network.h"
#include "c6_voice.h"
#include "cJSON.h"
#include "generated/c6_catalog.h"
#include "driver/usb_serial_jtag.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "taby_eye_motion.h"
#include "taby_reusable_preview.h"
#include "taby_reusable_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "taby_animation_assets.h"
#include "taby_line_reader.h"
#include "taby_state_machine.h"
#include "taby_transport_protocol.h"

static const uint8_t *assets;
static uint8_t *pixels;
static const c6_clip_t *clip, *sequence_next;
static size_t cursor;
static uint16_t frame;
static uint16_t clip_frames;
static bool clip_loop;
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
static c6_preferences_t preferences;
static bool preferences_ready;
static bool network_ready;
static uint8_t active_transport;
static uint32_t idle_pass;
static bool voice_mode;
static int64_t voice_due;
static uint32_t voice_frames;
typedef struct {
    char *command,*response;
    size_t response_capacity,used;
    bool overflow;
    unsigned references;
    SemaphoreHandle_t complete;
} c6_request_t;
static QueueHandle_t requests;
static TaskHandle_t owner;
static c6_request_t *active_request;
static portMUX_TYPE request_lock=portMUX_INITIALIZER_UNLOCKED;
static void handle_command(const char *line);

static void release_request(c6_request_t *request) {
    portENTER_CRITICAL(&request_lock);
    bool destroy=--request->references==0;
    portEXIT_CRITICAL(&request_lock);
    if(destroy){vSemaphoreDelete(request->complete);free(request);}
}
static void execute_request(c6_request_t *request) {
    c6_request_t *previous=active_request;
    active_request=request;
    handle_command(request->command);
    active_request=previous;
    if(request->overflow)snprintf(request->response,request->response_capacity,"TABY:ERR response_too_large");
}
bool c6_runtime_request(const char *command,char *response,size_t capacity,uint32_t timeout_ms) {
    if(!command || !response || capacity<2 || !requests)return false;
    size_t command_length=strnlen(command,1024);
    if(command_length>=1024)return false;
    for(const unsigned char *p=(const unsigned char*)command;*p;++p)
        if(*p<0x20 || *p==0x7f)return false;
    size_t response_capacity=capacity<4096?capacity:4096;
    /* One bounded allocation owns the exact command and the caller's capped
       response, so short polling requests do not reserve a full catalog. */
    c6_request_t *request=calloc(1,sizeof(*request)+command_length+1+response_capacity);
    if(!request)return false;
    request->command=(char*)(request+1);
    request->response=request->command+command_length+1;
    request->response_capacity=response_capacity;
    request->complete=xSemaphoreCreateBinary();
    if(!request->complete){free(request);return false;}
    strcpy(request->command,command);
    bool inline_call=xTaskGetCurrentTaskHandle()==owner;
    request->references=inline_call?1:2;
    bool done;
    if(inline_call){execute_request(request);done=true;}
    else if(xQueueSend(requests,&request,0)!=pdTRUE) {
        release_request(request);release_request(request);return false;
    } else done=xSemaphoreTake(request->complete,pdMS_TO_TICKS(timeout_ms))==pdTRUE;
    bool success=done && !request->overflow && strlen(request->response)<capacity;
    if(success)strcpy(response,request->response);
    else snprintf(response,capacity,"TABY:ERR runtime_unavailable");
    release_request(request);
    return success;
}

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1]<<8); }
static uint32_t u32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void reply(const char *text) {
    if(active_request) {
        size_t n=strlen(text);
        size_t remaining=active_request->response_capacity-active_request->used;
        size_t overhead=active_request->used?2:1; /* terminator and optional newline */
        if(remaining<overhead || n>remaining-overhead){active_request->overflow=true;return;}
        if(active_request->used)active_request->response[active_request->used++]='\n';
        memcpy(active_request->response+active_request->used,text,n+1);
        active_request->used+=n;
        return;
    }
    usb_serial_jtag_write_bytes(text, strlen(text), pdMS_TO_TICKS(100));
    usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(100));
}
static const uint8_t *clip_data(const c6_clip_t *selected, size_t offset, size_t length) {
    if (!selected || offset>selected->size || length>selected->size-offset) return NULL;
    if (c6_sd_owns(selected)) return c6_sd_read(selected, offset, length);
    if (selected->offset>C6_ASSET_BYTES || selected->size>C6_ASSET_BYTES-selected->offset) return NULL;
    return assets+selected->offset+offset;
}
static const c6_clip_t *find_clip(const char *id) {
    taby_animation_asset_t canonical;
    if (!id) return NULL;
    if (taby_animation_asset_for_id(id, &canonical)) id = canonical.animation_id;
    for (size_t i=0; i<c6_sd_count(); ++i) {
        const c6_clip_t *candidate = c6_sd_at(i);
        if (!strcmp(candidate->id,id)) return candidate;
    }
    for (size_t i=0; i<C6_CLIP_COUNT; ++i) {
        if (strcmp(id, c6_clips[i].id) == 0) return &c6_clips[i];
    }
    return NULL;
}
static bool select_clip(const c6_clip_t *selected) {
    if (display_fault || !c6_board_healthy() || !selected || selected->size < 12) return false;
    const uint8_t *header = clip_data(selected,0,12);
    if (!header || memcmp(header, "TC6A", 4) || u16(header+4) != C6_LCD_WIDTH ||
        u16(header+6) != C6_FRAME_HEIGHT || u16(header+8) == 0 || u16(header+10) > 1) return false;
    clip_frames = u16(header+8);
    clip_loop = u16(header+10) != 0;
    clip = selected;
    cursor = 12;
    frame = 0;
    due = 0;
    return true;
}
/* A card can disappear after its pack CRC passed. Remember ownership before
   reading: the read itself may disable SD. Re-resolve against validated flash. */
static bool select_clip_id(const char *id) {
    const c6_clip_t *selected=find_clip(id);
    bool from_sd=selected && c6_sd_owns(selected);
    if(select_clip(selected))return true;
    if(!from_sd)return false;
    c6_sd_disable();
    return select_clip(find_clip(id));
}
static bool available(const char *id) { return find_clip(id) != NULL; }
static const char *state_name(void) {
    const char *ui=c6_ui_state_name();
    return ui?ui:taby_transport_state_name(machine.current_state);
}
static bool idle_backing(void) {
    if (!select_clip_id("idle_01_loop")) return false;
    sequence_next = NULL;
    c6_voice_reset();voice_mode=false;
    machine.current_state = TABY_STATE_AMBIENT_IDLE;
    return c6_board_healthy();
}
static bool clear(void) {
    if(!idle_backing())return false;
    if(c6_ui_active())c6_ui_hide();
    return c6_board_healthy();
}
static bool apply(const taby_transport_resolution_t *resolution) {
    const char *required[2];
    size_t count = taby_transport_required_animations(resolution, required);
    if (!count || !select_clip_id(required[0])) return false;
    if(c6_ui_active())c6_ui_hide();
    c6_voice_reset();voice_mode=false;
    sequence_next = count > 1 ? find_clip(required[1]) : NULL;
    taby_state_machine_apply_command(&machine, resolution->command);
    return c6_board_healthy();
}
static const taby_transport_display_hooks_t hooks = {
    .animation_available=available, .apply=apply, .state_name=state_name, .clear=clear,
};

static void voice_playback(int64_t now) {
    if(!voice_mode || now<voice_due || display_fault)return;
    if(!c6_voice_active(now/1000.0)){clear();return;}
    const c6_clip_t *neutral=NULL;
    for(size_t i=0;i<C6_CLIP_COUNT;++i)if(!strcmp(c6_clips[i].id,"idle_01_loop")){neutral=&c6_clips[i];break;}
    if(!neutral || neutral->size<18)goto corrupt;
    const uint8_t *data=assets+neutral->offset;
    uint32_t length=u32(data+14);
    if(length<128 || length>neutral->size-18 ||
        !c6_rle_decode(data+18+128,length-128,data+18,pixels,C6_LCD_WIDTH*C6_FRAME_HEIGHT) ||
        !c6_voice_render(pixels,C6_LCD_WIDTH*C6_FRAME_HEIGHT*2,C6_LCD_WIDTH,C6_FRAME_HEIGHT,now/1000.0))goto corrupt;
    if(c6_board_draw_animation(pixels)!=ESP_OK){++display_errors;display_fault=true;return;}
    ++voice_frames;++rendered;voice_due=now+33333;
    return;
corrupt:
    ++decode_errors;clear();
}

static void playback(int64_t now) {
    if(voice_mode){voice_playback(now);return;}
    if (c6_ui_active() || !clip || now < due) return;
    bool gentle=preferences.eye_motion!=TABY_EYE_MOTION_NORMAL && !strcmp(clip->id,"idle_01_loop");
    /* Converted samples 0..4 contain a complete centred blink; 0..20 add
       the original small tilt and stop before its long side looks. */
    uint16_t limit=gentle ? (preferences.eye_motion==TABY_EYE_MOTION_CALM && idle_pass%6==5?21:5) : clip_frames;
    if (frame >= limit) {
        ++cycles;
        if(gentle)++idle_pass;
        if (sequence_next) {
            const c6_clip_t *next = sequence_next;
            sequence_next = NULL;
            if(!select_clip_id(next->id))goto corrupt;
        } else if (clip_loop) {
            cursor = 12;
            frame = 0;
        } else {
            clear();
        }
    }
    if (cursor > clip->size || clip->size-cursor < 6) goto corrupt;
    const uint8_t *header = clip_data(clip,cursor,6);
    if (!header) goto corrupt;
    uint16_t duration = u16(header);
    uint32_t length = u32(header+2);
    cursor += 6;
    if (!duration || length < 128 || length > clip->size-cursor) goto corrupt;
    if(c6_sd_owns(clip)) {
        if(!c6_sd_decode(clip,cursor,length,pixels,C6_LCD_WIDTH*C6_FRAME_HEIGHT))goto corrupt;
    }else {
        const uint8_t *data = clip_data(clip,cursor,length);
        if (!data || !c6_rle_decode(data+128,length-128,data,pixels,C6_LCD_WIDTH*C6_FRAME_HEIGHT))goto corrupt;
    }
    if (c6_board_draw_animation(pixels) != ESP_OK) {
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
    if(gentle && frame==1)due=now+(int64_t)(preferences.eye_motion==TABY_EYE_MOTION_STILL?
            4000+esp_random()%5001:3000+esp_random()%4001)*1000;
    return;
corrupt:
    ++decode_errors;
    /* Removing/breaking a card falls back to the independently validated flash pack. */
    if (clip && c6_sd_owns(clip)) { c6_sd_disable(); clear(); }
    else clip = NULL;
}

static void touch_poll(int64_t now) {
    bool pressed;
    esp_err_t err = c6_board_touch(&pressed, &touch_x, &touch_y);
    if (err != ESP_OK) { ++touch_errors; return; }
    c6_ui_touch(pressed,touch_x,touch_y);
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

static bool preference_command(const char *command,char *response,size_t capacity) {
    c6_preferences_t next;
    c6_pref_result_t result=c6_preferences_command(&preferences,command,&next,response,capacity);
    if(result==C6_PREF_UNHANDLED)return false;
    if(result==C6_PREF_CHANGED) {
        if(next.orientation==360 && !c6_board_status()->imu_available) {
            snprintf(response,capacity,"TABY:ERR display_orientation_auto_unsupported");return true;
        }
        uint16_t old_rotation=c6_board_status()->rotation;
        if(next.orientation!=360 && c6_board_set_rotation(next.orientation)!=ESP_OK) {
            snprintf(response,capacity,"TABY:ERR display_orientation_set_failed");return true;
        }
        c6_board_brightness(next.brightness_percent);
        if(!preferences_ready || c6_preferences_save(&next)!=ESP_OK) {
            c6_board_brightness(preferences.brightness_percent);
            c6_board_set_rotation(old_rotation);
            c6_ui_refresh_orientation();
            snprintf(response,capacity,"TABY:ERR preferences_save_failed");return true;
        }
        preferences=next;brightness=next.brightness_percent;
        if(c6_board_status()->rotation!=old_rotation)c6_ui_refresh_orientation();
        if(clip && !strcmp(clip->id,"idle_01_loop")){select_clip(clip);idle_pass=0;}
        due=0;
    }
    if(result!=C6_PREF_INVALID && !strncmp(command,"DISPLAY_ORIENTATION",19)) {
        uint16_t mode=preferences.orientation;
        const char *name=c6_preferences_orientation_name(mode);
        uint16_t effective=c6_board_status()->rotation;
        const char *orientation=c6_preferences_orientation_name(effective);
        snprintf(response,capacity,"TABY:DISPLAY_ORIENTATION {\"mode\":\"%s\",\"orientation\":\"%s\",\"rotation_degrees\":%u,\"auto_supported\":%s,\"display_shape\":\"rectangle\"}",
            name,orientation,effective,c6_board_status()->imu_available?"true":"false");
    }
    return true;
}

static void handle_command(const char *line) {
    char response[2048];
    if (strcmp(line, "PING") == 0) {
        reply("TABY:PONG");
    } else if(!strcmp(line,"STATE")) {
        snprintf(response,sizeof(response),"TABY:STATE %s",state_name());reply(response);
    } else if(!strcmp(line,"POWER")) {
        snprintf(response,sizeof(response),"TABY:POWER {\"power_voltage_mv\":%d,\"battery_percent\":-1,\"external_power\":%s}",
            c6_board_status()->battery_mv,usb_serial_jtag_is_connected()?"true":"false");reply(response);
    } else if(!strcmp(line,"TOUCH")) {
        snprintf(response,sizeof(response),"TABY:TOUCH %" PRIu32,touches);reply(response);
    } else if (strcmp(line, "INFO") == 0) {
        c6_network_status_t net={0};
        c6_network_get_status(&net);
        cJSON *info=cJSON_CreateObject();
        if(!info){reply("TABY:ERR no_memory");return;}
#define STR(k,v) cJSON_AddStringToObject(info,k,v)
#define NUM(k,v) cJSON_AddNumberToObject(info,k,v)
#define BOOL(k,v) cJSON_AddBoolToObject(info,k,v)
        STR("firmware_version","0.2.0-c6-dev.1");
        STR("assets_version",c6_sd_ready()?"0.2.0-sd":"0.1.0-dev");
        STR("hardware_target","c6-lcd-1.47");STR("display_shape","rectangle");
        NUM("display_width",172);NUM("display_height",320);
        STR("device_id",device_id);STR("identity_source","development_chip_mac");
        const char *modes[]={"usb","wifi","ble"};
        STR("preferred_transport",modes[preferences.preferred_transport]);
        STR("transport_mode",modes[active_transport]);
        BOOL("transport_onboarding_complete",true);BOOL("usb_bridge_ready",true);
        BOOL("development_build",true);BOOL("connected",net.wifi_connected);
        STR("wifi_mode",net.ap_active?(net.wifi_connected?"apsta":"ap"):(net.wifi_started?"sta":"off"));
        STR("ip",net.ip);STR("wifi_ssid",net.station_ssid);
        BOOL("wifi_started",net.wifi_started);BOOL("setup_ap_active",net.ap_active);
        BOOL("ble_started",net.ble_started);BOOL("ble_advertising",net.ble_advertising);
        BOOL("ble_connected",net.ble_connected);STR("network_error",net.last_error);
        const char *caps[]={"info","diag","touch","gesture","clear","brightness","eye_motion",
            "display_orientation","transport_default","reusable_ui","unsupported_animation","power","voice_motion"};
        cJSON *capabilities=cJSON_CreateStringArray(caps,sizeof(caps)/sizeof(caps[0]));
        if(capabilities && network_ready){cJSON_AddItemToArray(capabilities,cJSON_CreateString("wifi_setup"));
            cJSON_AddItemToArray(capabilities,cJSON_CreateString("wifi"));cJSON_AddItemToArray(capabilities,cJSON_CreateString("ble"));}
        cJSON_AddItemToObject(info,"capabilities",capabilities);
        STR("voice_renderer","raster_mouth");BOOL("voice_pose_animation",false);
        NUM("animation_count",c6_sd_ready()?c6_sd_count():C6_CLIP_COUNT);
        STR("assets_sha256",c6_sd_ready()?c6_sd_sha256():C6_ASSET_SHA256);
        STR("asset_storage",c6_sd_ready()?"sd_card":"flash");STR("sd_status",c6_sd_status());
        NUM("brightness_percent",preferences.brightness_percent);STR("eye_motion",taby_eye_motion_name(preferences.eye_motion));
        NUM("rotation_degrees",c6_board_status()->rotation);BOOL("display_orientation_auto_supported",c6_board_status()->imu_available);
        NUM("power_voltage_mv",c6_board_status()->battery_mv);BOOL("battery_calibrated",c6_board_status()->battery_calibrated);
        BOOL("preferences_storage_ready",preferences_ready);
#undef STR
#undef NUM
#undef BOOL
        memcpy(response,"TABY:INFO ",10);
        if(!cJSON_PrintPreallocated(info,response+10,sizeof(response)-10,false))
            snprintf(response,sizeof(response),"TABY:ERR info_too_large");
        cJSON_Delete(info);
        reply(response);
    } else if (strcmp(line, "DIAG") == 0) {
        c6_voice_snapshot_t voice={0};c6_voice_snapshot(esp_timer_get_time()/1000.0,&voice);
        snprintf(response, sizeof(response),
            "TABY:DIAG {\"animation\":\"%s\",\"state\":\"%s\",\"frame\":%u,"
            "\"rendered_frames\":%" PRIu32 ",\"completed_cycles\":%" PRIu32 ","
            "\"decode_errors\":%" PRIu32 ",\"display_errors\":%" PRIu32 ","
            "\"free_heap\":%u,\"minimum_free_heap\":%u,\"largest_free_block\":%u,"
            "\"touch_samples\":%" PRIu32 ",\"touch_errors\":%" PRIu32 ","
            "\"touch_count\":%" PRIu32 ",\"touch_pressed\":%s,\"touch_x\":%u,\"touch_y\":%u,"
            "\"voice_active\":%s,\"voice_frames\":%" PRIu32 ",\"viseme\":%u,\"jaw_open\":%u}",
            clip ? clip->id : "", state_name(), frame, rendered, cycles, decode_errors, display_errors,
            (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), touch_samples,
            touch_errors, touches, finger_down ? "true" : "false", touch_x, touch_y,
            voice.active?"true":"false",voice_frames,voice.viseme,(unsigned)voice.jaw_open);
        reply(response);
    } else if (strcmp(line, "ANIMATIONS") == 0) {
        reply("TABY:ANIMATIONS_BEGIN");
        size_t count = c6_sd_ready()?c6_sd_count():C6_CLIP_COUNT;
        for (size_t i=0; i<count; ++i) {
            const c6_clip_t *entry = c6_sd_ready()?c6_sd_at(i):&c6_clips[i];
            snprintf(response, sizeof(response), "TABY:ANIMATION %s", entry->id);
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
        taby_reusable_choice_signal_t choice;
        taby_reusable_ui_read_choice_signal(&choice);
        snprintf(response,sizeof(response),"TABY:CHOICE_SIGNAL {\"signal\":%" PRIu32 ",\"selection\":\"%s\"}",
                 choice.signal,taby_reusable_ui_choice_selection_name(choice.selection));
        reply(response);
    } else if (!strncmp(line, "TRANSPORT_MODE ", 15)) {
        const char *mode = strchr(line, ' ')+1;
        int selected=!strcmp(mode,"usb")?0:!strcmp(mode,"wifi")?1:
            (!strcmp(mode,"ble")||!strcmp(mode,"bluetooth"))?2:-1;
        if(selected<0)reply("TABY:ERR unsupported_transport");
        else if(!network_ready || c6_network_activate((uint8_t)selected)!=ESP_OK)reply("TABY:ERR transport_start_failed");
        else {active_transport=(uint8_t)selected;reply("TABY:OK TRANSPORT_MODE");}
    } else {
        const char *command = !strncmp(line, "CMD ", 4) ? line+4 : line;
        if(!strncmp(command,"VOICE_MOTION ",13)) {
            double now=esp_timer_get_time()/1000.0;
            if(!c6_voice_command(command+13,now))reply("TABY:ERR voice_motion_invalid");
            else {
                if(c6_voice_active(now)){
                    if(c6_ui_active())c6_ui_hide();
                    voice_mode=true;clip=NULL;sequence_next=NULL;voice_due=0;
                    machine.current_state=TABY_STATE_VOICE_TALKING;
                }else if(voice_mode)clear();
                reply("TABY:OK VOICE_MOTION");
            }
            return;
        }
        if(c6_network_command(command,response,sizeof(response))) {
            reply(response);return;
        }
        if(preference_command(command,response,sizeof(response))) {
            reply(response);return;
        }
        if(!strncmp(command,"UI/",3)) {
            char error[128];
            if(taby_reusable_preview_render_ui_command(command,error,sizeof(error))) {
                /* Keep the new card visible while preparing what resumes at
                   expiry; clear() would hide this successful presentation. */
                if(voice_mode)idle_backing();
                c6_voice_reset();voice_mode=false;
                snprintf(response,sizeof(response),"TABY:OK %s",state_name());
            }
            else snprintf(response,sizeof(response),"TABY:ERR ui_command_failed %.127s",error);
        } else {
            taby_transport_handle_display_command(command, &hooks, "TABY:", true, response, sizeof(response));
        }
        reply(response);
    }
}

void app_main(void) {
    owner=xTaskGetCurrentTaskHandle();
    requests=xQueueCreate(4,sizeof(c6_request_t*));
    ESP_ERROR_CHECK(requests?ESP_OK:ESP_ERR_NO_MEM);
    preferences_ready=nvs_flash_init()==ESP_OK;
    c6_preferences_defaults(&preferences);
    if(preferences_ready)c6_preferences_load(&preferences);
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(device_id, sizeof(device_id), "taby-c6-%02x%02x%02x%02x%02x%02x",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    ESP_ERROR_CHECK(c6_board_bus_init());
    /* Establish SPI mode on an inserted SD card before driving other SPI devices. */
    c6_sd_init();
    ESP_ERROR_CHECK(c6_board_init());
    brightness=preferences.brightness_percent;
    c6_board_brightness(brightness);
    if(preferences.orientation!=360)ESP_ERROR_CHECK(c6_board_set_rotation(preferences.orientation));
    c6_ui_init();
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "assets");
    ESP_ERROR_CHECK(partition && C6_ASSET_BYTES <= partition->size ? ESP_OK : ESP_ERR_NOT_FOUND);
    esp_partition_mmap_handle_t mapping;
    ESP_ERROR_CHECK(esp_partition_mmap(partition, 0, C6_ASSET_BYTES, ESP_PARTITION_MMAP_DATA,
                                     (const void **)&assets, &mapping));
    ESP_ERROR_CHECK(esp_crc32_le(0, assets, C6_ASSET_BYTES) == C6_ASSET_CRC32 ? ESP_OK : ESP_ERR_INVALID_CRC);
    pixels = heap_caps_malloc(C6_LCD_WIDTH*C6_FRAME_HEIGHT*2, MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    ESP_ERROR_CHECK(pixels ? ESP_OK : ESP_ERR_NO_MEM);
    taby_state_machine_init(&machine);
    ESP_ERROR_CHECK(select_clip_id("startup") ? ESP_OK : ESP_ERR_INVALID_SIZE);
    machine.current_state = TABY_STATE_AMBIENT_STARTUP;
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size=4096, .rx_buffer_size=4096};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    char line[1024];
    taby_line_reader_t reader;
    taby_line_reader_init(&reader, line, sizeof(line));
    network_ready=preferences_ready && c6_network_init(device_id)==ESP_OK;
    if(network_ready && c6_network_activate(preferences.preferred_transport)==ESP_OK)
        active_transport=preferences.preferred_transport;
    int64_t next_touch = 0;
    while (true) {
        uint8_t incoming[128];
        int count = usb_serial_jtag_read_bytes(incoming, sizeof(incoming), pdMS_TO_TICKS(2));
        for (int i=0; i<count; ++i) {
            taby_line_reader_event_t event = taby_line_reader_push(&reader, incoming[i]);
            if (event == TABY_LINE_READER_LINE) handle_command(reader.buffer);
            else if (event == TABY_LINE_READER_TOO_LONG) reply("TABY:ERR line_too_long");
        }
        for(unsigned i=0;i<2;++i) {
            c6_request_t *request=NULL;
            if(xQueueReceive(requests,&request,0)!=pdTRUE)break;
            execute_request(request);
            xSemaphoreGive(request->complete);
            release_request(request);
        }
        int64_t now = esp_timer_get_time();
        playback(now);
        if(c6_board_poll(now,preferences.orientation==360)){c6_ui_refresh_orientation();due=0;}
        if (now >= next_touch) { touch_poll(now); next_touch = now+20000; }
        c6_ui_tick(now);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
