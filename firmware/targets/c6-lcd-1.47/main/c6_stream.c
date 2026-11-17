#include "c6_stream.h"

typedef struct {
    c6_stream_read_t read;
    void *context;
    uint8_t *buffer;
    size_t capacity, size, consumed, cursor, buffered;
} stream_t;

static bool next_byte(stream_t *stream, uint8_t *value) {
    if (stream->cursor == stream->buffered) {
        size_t count = stream->size - stream->consumed;
        if (!count) return false;
        if (count > stream->capacity) count = stream->capacity;
        if (!stream->read(stream->context, stream->consumed, stream->buffer, count)) return false;
        stream->cursor = 0;
        stream->buffered = count;
    }
    *value = stream->buffer[stream->cursor++];
    ++stream->consumed;
    return true;
}

bool c6_stream_decode(c6_stream_read_t read, void *context, size_t encoded_size,
                       const uint8_t *palette, uint8_t *out, size_t pixels,
                       uint8_t *scratch, size_t scratch_size) {
    if (!read || !palette || !out || !scratch || !scratch_size ||
        scratch_size > 4096 || !pixels || pixels > SIZE_MAX/2) return false;
    stream_t stream = {
        .read=read, .context=context, .buffer=scratch,
        .capacity=scratch_size, .size=encoded_size,
    };
    size_t written = 0;
    while (stream.consumed < encoded_size && written < pixels) {
        uint8_t token;
        if (!next_byte(&stream, &token)) return false;
        size_t count = (token & 127u) + 1u;
        if (count > pixels-written) return false;
        bool repeat = (token & 128u) != 0;
        size_t bytes = repeat ? 1u : count;
        if (bytes > encoded_size-stream.consumed) return false;
        uint8_t index = 0;
        for (size_t i=0; i<count; ++i) {
            if ((!repeat || i == 0) && !next_byte(&stream, &index)) return false;
            if (index >= 64) return false;
            out[2*written] = palette[2*index];
            out[2*written+1] = palette[2*index+1];
            ++written;
        }
    }
    return stream.consumed == encoded_size && written == pixels;
}
