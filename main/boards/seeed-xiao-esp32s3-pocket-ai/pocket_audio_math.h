#pragma once

#include <algorithm>
#include <cstdint>

namespace pocket_audio {
// INMP441 sends signed 24-bit PCM in bits 31..8 of a Philips 32-bit slot.
// Arithmetic right shift keeps the sign and discards only the low eight PCM bits.
inline int16_t MicrophonePcm(int32_t slot) { return static_cast<int16_t>(slot >> 16); }

inline int32_t GainQ16(int volume) {
    volume = std::clamp(volume, 0, 30);
    return volume * volume * 65536 / 10000;
}

// Ramp up to 0.09 FS over approximately 25 ms at 48 kHz; never amplify above that cap.
inline int32_t RampGain(int32_t current, int32_t target) {
    constexpr int32_t step = 5;
    return current + std::clamp(target - current, -step, step);
}

inline int32_t SpeakerSlot(int16_t sample, int32_t gain) {
    return static_cast<int32_t>(static_cast<int64_t>(sample) * gain);
}
}  // namespace pocket_audio
