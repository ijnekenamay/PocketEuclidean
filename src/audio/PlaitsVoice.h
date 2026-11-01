#pragma once

#include "audio/SynthVoice.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace pocket::audio {

// Shared helpers for the Mutable Instruments voice wrappers.
inline bool mutableParametersValid(const SynthParameters& p) noexcept {
    return std::isfinite(p.pitchHz) && p.pitchHz > 0.0F &&
           std::isfinite(p.timbre) && p.timbre >= 0.0F && p.timbre <= 1.0F &&
           std::isfinite(p.color) && p.color >= 0.0F && p.color <= 1.0F &&
           std::isfinite(p.morph) && p.morph >= 0.0F && p.morph <= 1.0F &&
           std::isfinite(p.level) && p.level >= 0.0F && p.level <= 1.0F;
}

inline float midiNoteFromPitchHz(float hz) noexcept {
    return 69.0F + 12.0F * std::log2(hz / 440.0F);
}

// Per-track voice running the real Plaits DSP (third_party/mutable). Plaits
// is a 48 kHz design, so its output is resampled to the device rate.
//
// Parameter roles follow the hardware: color is HARMONICS, timbre is TIMBRE,
// morph is MORPH, and model selects the engine (0..23). A trigger drives the
// built-in decay envelope and low-pass gate, as with a patched TRIG input.
// All state lives in a heap block allocated at construction, so rendering
// never allocates and tracks never share state.
class PlaitsVoice final : public ISynthVoice {
public:
    PlaitsVoice();
    ~PlaitsVoice() override;
    PlaitsVoice(const PlaitsVoice&) = delete;
    PlaitsVoice& operator=(const PlaitsVoice&) = delete;

    bool setParameters(const SynthParameters& parameters) noexcept override;
    bool noteOn(float velocity) noexcept override;
    void noteOff() noexcept override;
    bool render(float* monoOutput,
                std::size_t frameCount,
                std::uint32_t sampleRate) noexcept override;
    bool isRealtimeSafe() const noexcept override { return true; }

    bool isActive() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pocket::audio
