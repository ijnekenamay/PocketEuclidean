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

// Second oscillator runs a few cents sharp. The slow beating between the two
// copies thickens a single-voice track without changing its perceived pitch.
constexpr float DetuneRatio = 1.0035F;

// One octave below the played note. It supplies the low body that a bare
// saw/pulse leaves out, which is what made the default patch sound thin.
constexpr float SubLevel = 0.30F;

// Split between the main and detuned oscillators before the sub is summed in.
constexpr float MainLevel = 0.72F;
constexpr float DetunedLevel = 0.28F;

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
    const float detuneStep = phaseStep * DetuneRatio;
    const float subStep = phaseStep * 0.5F;
    const float gain = active_ ? velocity_ * parameters_.level : 0.0F;
    const float brightness = parameters_.color * 0.25F;

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const float main =
            oscillator(phase_, phaseStep, parameters_.timbre,
                       parameters_.morph);
        const float detuned =
            oscillator(detunePhase_, detuneStep, parameters_.timbre,
                       parameters_.morph);
        float body = main * MainLevel + detuned * DetunedLevel;
        body += fastSine(subPhase_) * SubLevel;
        // Keep the summed body inside [-1, 1] so the shared VCA and the mixer
        // never see an oversized voice.
        body *= 1.0F / (MainLevel + DetunedLevel + SubLevel);

        float secondHarmonicPhase = phase_ * 2.0F;
        if (secondHarmonicPhase >= 1.0F) {
            secondHarmonicPhase -= 1.0F;
        }
        const float wave =
            body * (1.0F - brightness) +
            fastSine(secondHarmonicPhase) * brightness;

        monoOutput[frame] = wave * gain;

        phase_ += phaseStep;
        if (phase_ >= 1.0F) {
            phase_ -= 1.0F;
        }
        detunePhase_ += detuneStep;
        if (detunePhase_ >= 1.0F) {
            detunePhase_ -= 1.0F;
        }
        subPhase_ += subStep;
        if (subPhase_ >= 1.0F) {
            subPhase_ -= 1.0F;
        }
    }
    return true;
}

float BasicVaVoice::oscillator(float phase, float phaseStep, float timbre,
                               float morph) noexcept {
    const float pulseWidth = 0.1F + timbre * 0.8F;
    const float morphPosition = morph * 3.0F;
    if (morphPosition < 1.0F) {
        const float blend = morphPosition;
        return fastSine(phase) * (1.0F - blend) +
               triangle(phase) * blend;
    }
    if (morphPosition < 2.0F) {
        const float blend = morphPosition - 1.0F;
        return triangle(phase) * (1.0F - blend) +
               saw(phase, phaseStep) * blend;
    }
    const float blend = morphPosition - 2.0F;
    return saw(phase, phaseStep) * (1.0F - blend) +
           pulse(phase, pulseWidth, phaseStep) * blend;
}

// PolyBLEP residual: a short polynomial correction applied around a
// discontinuity so the naive saw/pulse edges do not fold high harmonics back
// down into the audible band. This is what makes the raw waveforms read as
// "clear" instead of gritty.
float BasicVaVoice::polyBlep(float phase, float phaseStep) noexcept {
    if (phaseStep <= 0.0F) {
        return 0.0F;
    }
    if (phase < phaseStep) {
        const float t = phase / phaseStep;
        return t + t - t * t - 1.0F;
    }
    if (phase > 1.0F - phaseStep) {
        const float t = (phase - 1.0F) / phaseStep;
        return t * t + t + t + 1.0F;
    }
    return 0.0F;
}

float BasicVaVoice::triangle(float phase) noexcept {
    return 1.0F - 4.0F * std::abs(phase - 0.5F);
}

float BasicVaVoice::saw(float phase, float phaseStep) noexcept {
    return phase * 2.0F - 1.0F - polyBlep(phase, phaseStep);
}

float BasicVaVoice::pulse(float phase, float pulseWidth,
                          float phaseStep) noexcept {
    float value = phase < pulseWidth ? 1.0F : -1.0F;
    value += polyBlep(phase, phaseStep);
    float fallingPhase = phase - pulseWidth;
    if (fallingPhase < 0.0F) {
        fallingPhase += 1.0F;
    }
    value -= polyBlep(fallingPhase, phaseStep);
    return value;
}

} // namespace pocket::audio
