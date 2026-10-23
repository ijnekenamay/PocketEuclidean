#include "audio/BasicVaVoice.h"

#include <cmath>

namespace pocket::audio {
namespace {

float fastSine(float phase) noexcept {
    float x = phase * 2.0F;
    if (x > 1.0F) {
        x -= 2.0F;
    }
    return 4.0F * x * (1.0F - std::abs(x));
}

} // namespace

bool BasicVaVoice::setParameters(
    const SynthParameters& parameters) noexcept {
    if (!std::isfinite(parameters.pitchHz) ||
        parameters.pitchHz <= 0.0F ||
        parameters.pitchHz > SynthVoiceRack::MaxPitchHz ||
        !std::isfinite(parameters.timbre) ||
        parameters.timbre < 0.0F || parameters.timbre > 1.0F ||
        !std::isfinite(parameters.color) ||
        parameters.color < 0.0F || parameters.color > 1.0F ||
        !std::isfinite(parameters.morph) ||
        parameters.morph < 0.0F || parameters.morph > 1.0F ||
        !std::isfinite(parameters.level) ||
        parameters.level < 0.0F || parameters.level > 1.0F) {
        return false;
    }
    parameters_ = parameters;
    return true;
}

bool BasicVaVoice::noteOn(float velocity) noexcept {
    if (!std::isfinite(velocity) || velocity <= 0.0F || velocity > 1.0F) {
        return false;
    }
    velocity_ = velocity;
    active_ = true;
    return true;
}

void BasicVaVoice::noteOff() noexcept {
    active_ = false;
}

bool BasicVaVoice::render(float* monoOutput,
                          std::size_t frameCount,
                          std::uint32_t sampleRate) noexcept {
    if (monoOutput == nullptr ||
        frameCount > SynthVoiceRack::MaxRenderFrames ||
        sampleRate < 8000 || sampleRate > 192000 ||
        parameters_.pitchHz > static_cast<float>(sampleRate) * 0.5F) {
        return false;
    }

    const float phaseStep = parameters_.pitchHz /
                            static_cast<float>(sampleRate);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        monoOutput[frame] = oscillator(phase_, parameters_.timbre,
                                       parameters_.morph) *
                            (active_ ? velocity_ * parameters_.level : 0.0F);
        phase_ += phaseStep;
        if (phase_ >= 1.0F) {
            phase_ -= 1.0F;
        }
    }
    return true;
}

float BasicVaVoice::oscillator(float phase, float timbre,
                               float morph) const noexcept {
    const float pulseWidth = 0.1F + timbre * 0.8F;
    const float morphPosition = morph * 3.0F;
    float wave = 0.0F;
    if (morphPosition < 1.0F) {
        const float blend = morphPosition;
        wave = fastSine(phase) * (1.0F - blend) +
               triangle(phase) * blend;
    } else if (morphPosition < 2.0F) {
        const float blend = morphPosition - 1.0F;
        wave = triangle(phase) * (1.0F - blend) +
               saw(phase) * blend;
    } else {
        const float blend = morphPosition - 2.0F;
        wave = saw(phase) * (1.0F - blend) +
               pulse(phase, pulseWidth) * blend;
    }
    float secondHarmonicPhase = phase * 2.0F;
    if (secondHarmonicPhase >= 1.0F) {
        secondHarmonicPhase -= 1.0F;
    }
    const float brightness = parameters_.color * 0.25F;
    return wave * (1.0F - brightness) +
           fastSine(secondHarmonicPhase) * brightness;
}

float BasicVaVoice::triangle(float phase) noexcept {
    return 1.0F - 4.0F * std::abs(phase - 0.5F);
}

float BasicVaVoice::saw(float phase) noexcept {
    return phase * 2.0F - 1.0F;
}

float BasicVaVoice::pulse(float phase, float pulseWidth) noexcept {
    return phase < pulseWidth ? 1.0F : -1.0F;
}

} // namespace pocket::audio
