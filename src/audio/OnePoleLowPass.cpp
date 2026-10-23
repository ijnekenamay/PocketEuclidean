#include "audio/OnePoleLowPass.h"

#include <cmath>

namespace pocket::audio {
namespace {

constexpr float TwoPi = 6.28318530717958647692F;

} // namespace

bool OnePoleLowPass::setCutoff(float cutoffHz,
                               std::uint32_t sampleRate,
                               bool enabled) noexcept {
    if (!std::isfinite(cutoffHz) || cutoffHz < MinCutoffHz ||
        cutoffHz > MaxCutoffHz || sampleRate < 8000 ||
        sampleRate > 192000) {
        return false;
    }
    const float limitedCutoff =
        cutoffHz < static_cast<float>(sampleRate) * 0.45F
            ? cutoffHz
            : static_cast<float>(sampleRate) * 0.45F;
    const float angularFrequency = TwoPi * limitedCutoff;
    targetCoefficient_ = enabled
                             ? angularFrequency /
                                   (angularFrequency +
                                    static_cast<float>(sampleRate))
                             : 1.0F;
    coefficientRampFrames_ =
        sampleRate / 200U > 0 ? sampleRate / 200U : 1U;
    return true;
}

float OnePoleLowPass::process(float input) noexcept {
    if (coefficientRampFrames_ > 0) {
        coefficient_ += (targetCoefficient_ - coefficient_) /
                        static_cast<float>(coefficientRampFrames_);
        --coefficientRampFrames_;
        if (coefficientRampFrames_ == 0) {
            coefficient_ = targetCoefficient_;
        }
    }
    state_ += coefficient_ * (input - state_);
    return state_;
}

} // namespace pocket::audio
