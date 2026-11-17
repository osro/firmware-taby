#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "generated/c6_catalog.h"
bool c6_sd_init(void);
bool c6_sd_ready(void);
void c6_sd_disable(void);
const char *c6_sd_status(void);
const char *c6_sd_sha256(void);
size_t c6_sd_count(void);
const c6_clip_t *c6_sd_at(size_t index);
bool c6_sd_owns(const c6_clip_t *clip);
/* Small headers only (at most 4096 bytes); borrowed until the next SD operation. */
const uint8_t *c6_sd_read(const c6_clip_t *clip, size_t offset, size_t length);
/* Offset is clip-relative and length includes the 128-byte frame palette.
 * Uses the existing caller-owned RGB565 output buffer; no frame allocation.
 * Call from the same owner as c6_sd_read/init. Partial failed output is invalid. */
bool c6_sd_decode(const c6_clip_t *clip, size_t payload_offset, size_t payload_length,
                   uint8_t *out, size_t pixels);
