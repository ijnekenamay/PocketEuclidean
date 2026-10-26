#pragma once

#include "audio/SynthVoice.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket::audio {

// Multi-engine voice inspired by Mutable Instruments Plaits. The engine is
// selected from the morph control so a single continuous parameter can scan
// all sixteen models. All state is per-instance: voices on different tracks
// never share oscillators, noise generators, or envelopes.
class PlaitsVoice final : public ISynthVoice {
public:
    bool setParameters(const SynthParameters& parameters) noexcept override;
    bool noteOn(float velocity) noexcept override;
    void noteOff() noexcept override;
    bool render(float* monoOutput,
                std::size_t frameCount,
                std::uint32_t sampleRate) noexcept override;
    bool isRealtimeSafe() const noexcept override { return true; }

    bool isActive() const noexcept { return active_; }

private:
    void applyParameters(const SynthParameters& params) noexcept;

    SynthParameters parameters_{};
    float velocity_ = 0.0F;
    bool active_ = false;

    float enginePhase_ = 0.0F;
    // Seconds since note-on. Envelopes must not use the oscillator phase,
    // otherwise the amplitude restarts once per pitch cycle.
    float ageSeconds_ = 0.0F;
    int currentEngine_ = 0;

    // Per-engine state (formerly function-local statics shared by all tracks).
    float drumPhase_ = 0.0F;
    float particleAgeSeconds_ = 0.0F;
    std::array<float, 128> stringDelay_{};
    std::size_t stringDelayIndex_ = 0;
    float lowPassGateState_ = 0.0F;
    float noiseLowPassState_ = 0.0F;
    float highPassPreviousInput_ = 0.0F;
    float highPassState_ = 0.0F;
    std::uint32_t drumNoise_ = 0x12345678U;
    std::uint32_t snareNoise_ = 0x87654321U;
    std::uint32_t hiHatNoise_ = 0xDEADBEEFU;
    std::uint32_t coloredNoise_ = 0xCAFEBABEU;
    std::uint32_t particleNoise_ = 0x11223344U;
};

} // namespace pocket::audio
