#include "ui/StepLightEnvelope.h"

#include <cmath>

namespace pocket::ui {

bool StepLightEnvelope::trigger(double timestampSeconds) noexcept {
    if (!std::isfinite(timestampSeconds) ||
        (hasTriggered_ && timestampSeconds < triggeredAtSeconds_)) {
        return false;
    }

    float currentLuminance = 0.0F;
    if (hasTriggered_ && !valueAt(timestampSeconds, currentLuminance)) {
        return false;
    }

    triggeredAtSeconds_ = timestampSeconds;
    startingLuminance_ = currentLuminance;
    hasTriggered_ = true;
    return true;
}

bool StepLightEnvelope::valueAt(double timestampSeconds,
                                float& luminance) const noexcept {
    if (!std::isfinite(timestampSeconds) ||
        (hasTriggered_ && timestampSeconds < triggeredAtSeconds_)) {
        return false;
    }

    if (!hasTriggered_) {
        luminance = 0.0F;
        return true;
    }

    const double elapsed = timestampSeconds - triggeredAtSeconds_;
    if (elapsed < AttackSeconds) {
        const double progress = elapsed / AttackSeconds;
        const double attack = smoothStep(progress);
        luminance = static_cast<float>(
            startingLuminance_ +
            (1.0 - startingLuminance_) * attack);
        return true;
    }

    const double decayStartSeconds = AttackSeconds + PeakHoldSeconds;
    if (elapsed < decayStartSeconds) {
        luminance = 1.0F;
        return true;
    }

    const double decayElapsed = elapsed - decayStartSeconds;
    if (decayElapsed >= DecaySeconds) {
        luminance = 0.0F;
        return true;
    }

    const double decayProgress = decayElapsed / DecaySeconds;
    luminance = static_cast<float>(1.0 - smoothStep(decayProgress));
    return true;
}

double StepLightEnvelope::smoothStep(double value) noexcept {
    const double bounded = value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
    return bounded * bounded * (3.0 - 2.0 * bounded);
}

} // namespace pocket::ui
