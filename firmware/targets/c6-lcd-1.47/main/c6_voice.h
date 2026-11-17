#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    // sil, PP, FF, TH, DD, kk, CH, SS, nn, RR, aa, E, I, O, U.
    uint8_t viseme;
    float jaw_open; // 0..100, as in the shared parser.
    // idle, ask, oneHand, nod, shrug, hi, hello.
    uint8_t gesture;
    // idle, happy, serious, closeEye.
    uint8_t expression;
} c6_voice_snapshot_t;

// Single runtime-thread ownership, including command/active/snapshot/reset.
// Arguments follow "VOICE_MOTION ". Invalid input preserves the current talk.
// Times are finite monotonic milliseconds in [0, 1e12]; clockMs/atMs <=120000.
// At most 60 incoming frames and 3600 stored frames, matching upstream limits.
bool c6_voice_command(const char *args, double now_ms);
bool c6_voice_active(double now_ms);
bool c6_voice_snapshot(double now_ms, c6_voice_snapshot_t *out);
void c6_voice_reset(void);

// Overlay on a FRESHLY decoded idle_01_loop frame0, native 172x280 RGB565 BE.
// This buffer is sideways in asset space; existing display rotation follows.
// Edits are restricted to x26..70,y104..176. Silence/gaps/stops do not edit.
// All 15 visemes and jaw are supported; gestures/expressions are exposed in
// snapshots but their Rive body/eye poses are not drawn by this mouth overlay.
// No frame-buffer allocation, display calls, or ownership transfer.
// Returns false on bad geometry/time; the caller retains the buffer.
bool c6_voice_render(uint8_t *pixels, size_t bytes, uint16_t width,
                     uint16_t height, double now_ms);

#ifdef __cplusplus
}
#endif
