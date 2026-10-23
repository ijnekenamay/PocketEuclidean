#pragma once

#include <cstdint>

namespace pocket::audio {

class OnePoleLowPass {
public:
    static constexpr float MinCutoffHz = 100.0F;
    static constexpr float MaxCutoffHz = 20000.0F;

    bool setCutoff(float cutoffHz,
                   std::uint32_t sampleRate,
                   bool enabled = true) noexcept;
    float process(float input) noexcept;
    void reset() noexcept { state_ = 0.0F; }

private:
    float coefficient_ = 1.0F;
    float targetCoefficient_ = 1.0F;
    float state_ = 0.0F;
    std::uint32_t coefficientRampFrames_ = 0;
};

} // namespace pocket::audio
