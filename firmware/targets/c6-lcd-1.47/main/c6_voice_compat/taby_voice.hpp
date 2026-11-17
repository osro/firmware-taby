// Local build boundary for the unmodified upstream taby_voice.cpp. Keep the
// VoiceMotion declaration/member order identical to its upstream header; only
// SpeechFrame's Rive-bearing include is replaced by its portable definition.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace taby {
struct SpeechFrame {
    std::string viseme = "sil";
    float jawOpen = 0;
    std::string gesture = "idle";
    std::string expression = "idle";
};
class VoiceMotion {
public:
    bool take(const char* args, double nowMs);
    SpeechFrame at(double nowMs) const;
    bool active(double nowMs) const;
    SpeechFrame mouth(double nowMs, double talkingSinceMs) const;
    void reset();
private:
    struct Frame { uint8_t viseme; uint8_t jaw; uint8_t pose; };
    int m_talk = -1;
    bool m_stopped = true;
    double m_zeroAt = 0;
    std::vector<Frame> m_frames;
    std::vector<bool> m_have;
    double m_lastRunAt = -1e12;
};
SpeechFrame ownTalk(double ms);
} // namespace taby
