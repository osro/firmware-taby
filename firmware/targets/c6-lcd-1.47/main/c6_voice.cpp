#include "c6_voice.h"
#include "c6_voice_compat/taby_voice.hpp"

#include <cmath>
#include <cstring>

namespace {
taby::VoiceMotion voice;
constexpr const char *visemes[] = {"sil", "PP", "FF", "TH", "DD", "kk", "CH", "SS",
                                   "nn", "RR", "aa", "E", "I", "O", "U"};
constexpr const char *gestures[] = {"idle", "ask", "oneHand", "nod", "shrug", "hi", "hello"};
constexpr const char *expressions[] = {"idle", "happy", "serious", "closeEye"};

bool valid_time(double now) { return std::isfinite(now) && now >= 0 && now <= 1e12; }

bool number(const char *&at, unsigned limit, unsigned &out) {
    if (*at < '0' || *at > '9') return false;
    out = 0;
    do {
        const unsigned digit = static_cast<unsigned>(*at++ - '0');
        if (out > (limit - digit) / 10) return false;
        out = out * 10 + digit;
    } while (*at >= '0' && *at <= '9');
    return true;
}

int letter(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + c - 'a';
    if (c >= '0' && c <= '9') return 52 + c - '0';
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

// Upstream checks timeline bounds after changing the talk/clock. Check the
// complete bounded wire format first so rejected commands cannot erase voice.
bool valid_command(const char *args) {
    if (!args) return false;
    size_t length = 0;
    while (length <= 224 && args[length]) ++length;
    if (!length || length > 224) return false;
    const char *at = args;
    unsigned talk, clock, start;
    if (!number(at, 65535, talk) || *at++ != ' ') return false;
    if (std::strcmp(at, "stop") == 0) return true;
    if (!number(at, 120000, clock) || *at++ != ' ' ||
        !number(at, 120000, start) || *at++ != ' ') return false;
    const size_t letters = std::strlen(at);
    if (!letters || letters > 180 || letters % 3) return false;
    const unsigned first = (start * 30u + 500u) / 1000u;
    if (first + letters / 3 > 3600) return false;
    for (size_t i = 0; i < letters; i += 3) {
        const int v = letter(at[i]), jaw = letter(at[i + 1]), pose = letter(at[i + 2]);
        if (v < 0 || v >= 15 || jaw < 0 || pose < 0 || pose >= 28) return false;
    }
    return true;
}

template<size_t N>
uint8_t index_of(const std::string &name, const char *const (&names)[N]) {
    for (size_t i = 0; i < N; ++i) if (name == names[i]) return static_cast<uint8_t>(i);
    return 0;
}

void pixel(uint8_t *pixels, int u, int v, uint16_t color) {
    const int x = 50 - v, y = 140 + u;
    if (x < 26 || x > 70 || y < 104 || y > 176) return;
    const size_t offset = (static_cast<size_t>(y) * 172 + x) * 2;
    pixels[offset] = static_cast<uint8_t>(color >> 8);
    pixels[offset + 1] = static_cast<uint8_t>(color);
}

struct Mouth { int width, minimum, maximum; bool teeth, tongue; };
constexpr Mouth mouths[] = {
    {0,0,0,false,false}, // sil: preserve the original neutral smile.
    {22,1,2,false,false}, // PP: sealed lips, even at full jaw.
    {25,3,7,true,false},
    {23,4,9,true,true},
    {22,4,12,true,true},
    {20,5,14,false,false},
    {24,4,11,true,false},
    {26,3,6,true,false},
    {21,3,8,true,true},
    {17,5,12,true,true},
    {25,5,19,false,true},
    {28,4,13,true,false},
    {27,3,9,true,false},
    {16,5,18,false,false},
    {12,4,12,false,false},
};
} // namespace

extern "C" bool c6_voice_command(const char *args, double now_ms) {
    return valid_time(now_ms) && valid_command(args) && voice.take(args, now_ms);
}

extern "C" bool c6_voice_active(double now_ms) {
    return valid_time(now_ms) && voice.active(now_ms);
}

extern "C" bool c6_voice_snapshot(double now_ms, c6_voice_snapshot_t *out) {
    if (!out || !valid_time(now_ms)) return false;
    // Upstream at() casts the elapsed frame index to size_t before checking
    // bounds. Avoid that cast after completion, including long C6 uptimes
    // where the index would exceed the target's 32-bit size_t.
    const bool active = voice.active(now_ms);
    if (!active) {
        *out = {};
        return true;
    }
    const taby::SpeechFrame frame = voice.at(now_ms);
    *out = {active, index_of(frame.viseme, visemes), frame.jawOpen,
            index_of(frame.gesture, gestures), index_of(frame.expression, expressions)};
    return true;
}

extern "C" void c6_voice_reset(void) { voice.reset(); }

extern "C" bool c6_voice_render(uint8_t *pixels, size_t bytes, uint16_t width,
                                 uint16_t height, double now_ms) {
    if (!pixels || width != 172 || height != 280 || bytes < 172u * 280u * 2u ||
        !valid_time(now_ms)) return false;
    c6_voice_snapshot_t frame;
    if (!c6_voice_snapshot(now_ms, &frame)) return false;
    if (!frame.active || frame.viseme == 0) return true;
    const Mouth &mouth = mouths[frame.viseme];
    const int rx = mouth.width;
    const int ry = mouth.minimum + static_cast<int>((mouth.maximum - mouth.minimum) *
                                                   frame.jaw_open / 100.0f + 0.5f);
    // Erase only the inspected neutral mouth region; eyes and body stay intact.
    for (int u = -36; u <= 36; ++u)
        for (int v = -20; v <= 24; ++v) pixel(pixels, u, v, 0);
    if (ry <= 2) {
        for (int u = -rx; u <= rx; ++u) {
            pixel(pixels, u, 0, 0xffff);
            pixel(pixels, u, 1, 0xffff);
        }
        return true;
    }
    const int ix = rx - 2, iy = ry - 2;
    for (int u = -rx; u <= rx; ++u) {
        for (int v = -ry; v <= ry; ++v) {
            if (u*u*ry*ry + v*v*rx*rx > rx*rx*ry*ry) continue;
            const bool inner = u*u*iy*iy + v*v*ix*ix <= ix*ix*iy*iy;
            uint16_t color = inner ? 0 : 0xffff;
            if (inner && mouth.teeth && v <= -ry + 4) color = 0xffff;
            if (inner && mouth.tongue && v >= ry - 4 && u >= -rx/2 && u <= rx/2)
                color = 0xad55;
            pixel(pixels, u, v, color);
        }
    }
    return true;
}
