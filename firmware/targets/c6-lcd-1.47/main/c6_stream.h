#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A successful callback must fill size bytes at the encoded-stream-relative
 * offset. Reads are sequential, nonempty and no larger than scratch_size. */
typedef bool (*c6_stream_read_t)(void *context, size_t offset,
                                 uint8_t *out, size_t size);

/* Same tokens/output as c6_rle_decode. palette contains 128 RGB565 bytes.
 * The caller owns separate output, palette and scratch storage. Scratch must
 * contain 1..4096 bytes; the decoder allocates no memory. Failed decode may
 * leave partial output and must never be displayed. */
bool c6_stream_decode(c6_stream_read_t read, void *context, size_t encoded_size,
                       const uint8_t *palette, uint8_t *out, size_t pixels,
                       uint8_t *scratch, size_t scratch_size);
