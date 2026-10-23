#include "audio/AdEnvelope.h"

#include <cmath>

namespace pocket::audio {

bool AdEnvelope::trigger(const AdEnvelopeSettings& settings,
                         std::uint32_t sampleRate) noexcept {
    if (sampleRate < 8000 || sampleRate > 192000 ||
        !std::isfinite(settings.attackSeconds) ||
        settings.attackSeconds < AdEnvelopeSettings::MinAttackSeconds ||
        settings.attackSeconds > AdEnvelopeSettings::MaxAttackSeconds ||
        !std::isfinite(settings.decaySeconds) ||
        settings.decaySeconds < AdEnvelopeSettings::MinDecaySeconds ||
        settings.decaySeconds > AdEnvelopeSettings::MaxDecaySeconds) {
        return false;
    }
    attackStep_ = (1.0F - level_) /
                  (settings.attackSeconds * static_cast<float>(sampleRate));
    decayStep_ = 1.0F /
                 (settings.decaySeconds * static_cast<float>(sampleRate));
    settings_ = settings;
    sampleRate_ = sampleRate;
    stage_ = Stage::Attack;
    return true;
}

bool AdEnvelope::updateSettings(
    const AdEnvelopeSettings& settings) noexcept {
    if (sampleRate_ == 0 ||
        !std::isfinite(settings.attackSeconds) ||
        settings.attackSeconds < AdEnvelopeSettings::MinAttackSeconds ||
        settings.attackSeconds > AdEnvelopeSettings::MaxAttackSeconds ||
        !std::isfinite(settings.decaySeconds) ||
        settings.decaySeconds < AdEnvelopeSettings::MinDecaySeconds ||
        settings.decaySeconds > AdEnvelopeSettings::MaxDecaySeconds) {
        return false;
    }
    settings_ = settings;
    attackStep_ = (1.0F - level_) /
                  (settings_.attackSeconds * static_cast<float>(sampleRate_));
    decayStep_ = 1.0F /
                 (settings_.decaySeconds * static_cast<float>(sampleRate_));
    return true;
}

float AdEnvelope::process(float input) noexcept {
    if (stage_ == Stage::Attack) {
        level_ += attackStep_;
        if (level_ >= 1.0F) {
            level_ = 1.0F;
            stage_ = Stage::Decay;
        }
    } else if (stage_ == Stage::Decay) {
        level_ -= decayStep_;
        if (level_ <= 0.0F) {
            level_ = 0.0F;
            stage_ = Stage::Idle;
        }
    }
    return input * level_;
}

void AdEnvelope::reset() noexcept {
    level_ = 0.0F;
    stage_ = Stage::Idle;
    sampleRate_ = 0;
}

bool AdEnvelope::isActive() const noexcept {
    return stage_ != Stage::Idle;
}

} // namespace pocket::audio
