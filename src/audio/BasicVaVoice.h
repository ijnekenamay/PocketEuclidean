#pragma once

#include "audio/SynthVoice.h"

#include <cstddef>
#include <cstdint>

namespace pocket::audio {

class BasicVaVoice final : public ISynthVoice {
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
    float oscillator(float phase, float timbre, float morph) const noexcept;
    static float triangle(float phase) noexcept;
    static float saw(float phase) noexcept;
    static float pulse(float phase, float pulseWidth) noexcept;

    SynthParameters parameters_{};
    float phase_ = 0.0F;
    float velocity_ = 0.0F;
    bool active_ = false;
};

} // namespace pocket::audio
