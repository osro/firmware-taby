#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Palette entries and output pixels are RGB565 in LCD (big-endian) byte order. */
bool c6_rle_decode(const uint8_t *src, size_t size, const uint8_t *palette,
                   uint8_t *out, size_t pixels);
