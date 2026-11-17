/* Optional read-only SD library. Mount failures never format or erase media. */
#include "c6_sd.h"
#include "c6_file.h"
#include "c6_stream.h"
#include "c6_board.h"
#include "generated/c6_sd_catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "driver/sdspi_host.h"
#include "esp_crc.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define READ_BUFFER_SIZE 4096u
#define MAX_PAYLOAD (128u + C6_LCD_WIDTH*C6_FRAME_HEIGHT + (C6_LCD_WIDTH*C6_FRAME_HEIGHT+127u)/128u)
static FILE *pack;
static sdmmc_card_t *card;
static uint8_t *buffer;
static bool ready;
static const char *status = "not_checked";

static bool fail(const char *reason) {
    ready = false;
    status = reason;
    if (pack) { fclose(pack); pack = NULL; }
    free(buffer); buffer = NULL;
    return false;
}

#ifdef C6_SD_TRACE
/* Optional bounded initialization trace: protocol responses only, never CID,
   file data or credentials. Enable solely when diagnosing card bring-up. */
static unsigned trace_count;
static esp_err_t trace_transaction(int slot,sdmmc_command_t *command) {
    esp_err_t err=sdspi_host_do_transaction(slot,command);
    if((command->opcode==0 || command->opcode==8 || command->opcode==55 ||
        command->opcode==41 || command->opcode==58) && trace_count++<24)
        printf("C6SD command=%u arg=%08x err=%x response=%08x\n",
            (unsigned)command->opcode,(unsigned)command->arg,(unsigned)err,
            (unsigned)command->response[0]);
    return err;
}
#endif

bool c6_sd_init(void) {
    if (ready) return true;
    if (card) return false; /* Reboot for re-mount after media failure. */
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed=false, .max_files=2, .allocation_unit_size=4096,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
#ifdef C6_SD_TRACE
    trace_count=0;host.do_transaction=trace_transaction;
#endif
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = 4;
    if (esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount, &card) != ESP_OK) {
        card = NULL;
        return fail("unavailable");
    }
    struct stat st;
    if (stat("/sdcard/taby/assets.bin", &st) || st.st_size != C6_SD_ASSET_BYTES) return fail("pack_size_mismatch");
    pack = fopen("/sdcard/taby/assets.bin", "rb");
    if (!pack) return fail("pack_missing");
    buffer = malloc(READ_BUFFER_SIZE);
    if (!buffer) return fail("no_frame_memory");
    uint32_t crc = 0;
    size_t total = 0, count;
    while ((count=fread(buffer, 1, READ_BUFFER_SIZE, pack)) != 0) {
        crc = esp_crc32_le(crc, buffer, count);
        total += count;
        if ((total & 0xffffu) == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (ferror(pack) || total != C6_SD_ASSET_BYTES || crc != C6_SD_ASSET_CRC32) return fail("pack_checksum_mismatch");
    ready = true;
    status = "ready";
    return true;
}

bool c6_sd_ready(void) { return ready; }
void c6_sd_disable(void) { fail("invalid_frame"); }
const char *c6_sd_status(void) { return status; }
const char *c6_sd_sha256(void) { return C6_SD_ASSET_SHA256; }
size_t c6_sd_count(void) { return ready ? C6_SD_CLIP_COUNT : 0; }
const c6_clip_t *c6_sd_at(size_t index) {
    return ready && index<C6_SD_CLIP_COUNT ? &c6_sd_clips[index] : NULL;
}
bool c6_sd_owns(const c6_clip_t *clip) {
    for (size_t i=0; i<C6_SD_CLIP_COUNT; ++i) if (clip == &c6_sd_clips[i]) return true;
    return false;
}
static bool valid_clip(const c6_clip_t *clip) {
    return ready && pack && buffer && c6_sd_owns(clip) &&
        clip->offset<=C6_SD_ASSET_BYTES && clip->size<=C6_SD_ASSET_BYTES-clip->offset;
}
const uint8_t *c6_sd_read(const c6_clip_t *clip, size_t offset, size_t length) {
    if (!valid_clip(clip) || length>READ_BUFFER_SIZE) return NULL;
    if (!c6_file_read(pack, clip->offset, clip->size, offset, buffer, length)) {
        fail("read_failed");
        return NULL;
    }
    return buffer;
}

typedef struct {
    const c6_clip_t *clip;
    size_t offset, length;
} payload_t;

static bool read_payload(void *context, size_t offset, uint8_t *out, size_t size) {
    const payload_t *payload = context;
    if (offset>payload->length || size>payload->length-offset || size>READ_BUFFER_SIZE) return false;
    if (!c6_file_read(pack, payload->clip->offset, payload->clip->size,
                      payload->offset+offset, out, size)) return fail("read_failed");
    return true;
}

bool c6_sd_decode(const c6_clip_t *clip, size_t payload_offset, size_t payload_length,
                   uint8_t *out, size_t pixels) {
    if (!valid_clip(clip) || !out || !pixels || pixels>SIZE_MAX/2 ||
        payload_offset>clip->size || payload_length>clip->size-payload_offset ||
        payload_length<=128 || payload_length>MAX_PAYLOAD) return false;
    uint8_t palette[128];
    if (!c6_file_read(pack, clip->offset, clip->size, payload_offset, palette, sizeof(palette)))
        return fail("read_failed");
    payload_t payload = {clip, payload_offset+sizeof(palette), payload_length-sizeof(palette)};
    return c6_stream_decode(read_payload, &payload, payload.length, palette, out, pixels,
                            buffer, READ_BUFFER_SIZE);
}
