#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pocket {

// Converts the track's synth pitch (and the Turing semitone offset) into a
// MIDI note number. Out-of-range results are clamped to the MIDI range.
inline std::uint8_t midiNoteFromHz(float hz,
                                   int semitoneOffset = 0) noexcept {
    if (!(hz > 0.0F) || !std::isfinite(hz)) {
        return 60;
    }
    const double note = 69.0 +
                        12.0 * std::log2(static_cast<double>(hz) / 440.0) +
                        static_cast<double>(semitoneOffset);
    return static_cast<std::uint8_t>(
        std::clamp<long>(std::lround(note), 0, 127));
}

// Track volume drives note velocity. A zero volume still yields velocity 1;
// callers suppress the note entirely for muted or zero-volume tracks.
inline std::uint8_t midiVelocityFromVolume(
    std::uint8_t volumePercent) noexcept {
    const auto volume = std::min<std::uint32_t>(volumePercent, 100U);
    return static_cast<std::uint8_t>(
        std::clamp<std::uint32_t>((volume * 127U + 50U) / 100U, 1U, 127U));
}

} // namespace pocket
