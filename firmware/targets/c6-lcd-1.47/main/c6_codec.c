#include "c6_codec.h"
bool c6_rle_decode(const uint8_t *src, size_t size, const uint8_t *palette,
                   uint8_t *out, size_t pixels) {
    if (!src || !palette || !out || pixels == 0 || pixels > SIZE_MAX / 2) return false;
    size_t read = 0, written = 0;
    while (read < size && written < pixels) {
        uint8_t token = src[read++];
        size_t count = (token & 127u) + 1u;
        if (count > pixels - written) return false;
        bool repeat = (token & 128u) != 0;
        size_t bytes = repeat ? 1u : count;
        if (bytes > size - read) return false;
        for (size_t i = 0; i < count; ++i) {
            uint8_t index = src[read + (repeat ? 0 : i)];
            if (index >= 64) return false;
            out[2 * written] = palette[2 * index];
            out[2 * written + 1] = palette[2 * index + 1];
            ++written;
        }
        read += bytes;
    }
    return read == size && written == pixels;
}
