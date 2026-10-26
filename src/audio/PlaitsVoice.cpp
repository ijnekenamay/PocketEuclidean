#include "audio/PlaitsVoice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace pocket::audio {
namespace {

enum class EngineType : int {
    VirtualAnalog = 0,
    WaveTable = 1,
    FM = 2,
    Additive = 3,
    Chord = 4,
    Speech = 5,
    BassDrum = 6,
    SnareDrum = 7,
    HiHat = 8,
    Grain = 9,
    String = 10,
    Modal = 11,
    Noise = 12,
    Particle = 13,
    PhaseDistortion = 14,
    WaveShaping = 15,
};

// Parabolic bipolar approximation of sin(2*pi*phase). Cheap, bounded, and
// defined for any input because the phase is wrapped first.
inline float fastSine(float phase) noexcept {
    float x = (phase - std::floor(phase)) * 2.0F; // [0, 2)
    if (x >= 1.0F) {
        x -= 2.0F; // [-1, 1)
    }
    return 4.0F * x * (1.0F - std::abs(x));
}

// Schraudolph-style bit approximation of e^x for x <= 0. About 2-3% error,
// allocation-free and much cheaper than std::exp in the audio callback.
inline float fastExp(float x) noexcept {
    if (x <= -20.0F) {
        return 0.0F;
    }
    if (x >= 0.0F) {
        return 1.0F;
    }
    const float scaled = 12102203.0F * x + 1064866805.0F;
    const auto bits = static_cast<std::int32_t>(scaled);
    float result = 0.0F;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

inline float noiseFrom(std::uint32_t& state) noexcept {
    state = state * 1664525U + 1013904223U;
    return static_cast<float>(state >> 8) / 8388608.0F - 1.0F;
}

} // namespace
bool PlaitsVoice::setParameters(const SynthParameters& parameters) noexcept {
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
    applyParameters(parameters);
    return true;
}

void PlaitsVoice::applyParameters(const SynthParameters& params) noexcept {
    const float enginePosition = params.morph * 15.0F;
    currentEngine_ = std::clamp(static_cast<int>(std::floor(enginePosition)),
                                static_cast<int>(EngineType::VirtualAnalog),
                                static_cast<int>(EngineType::WaveShaping));
}

bool PlaitsVoice::noteOn(float velocity) noexcept {
    if (!std::isfinite(velocity) || velocity <= 0.0F || velocity > 1.0F) {
        return false;
    }
    velocity_ = velocity;
    active_ = true;
    enginePhase_ = 0.0F;
    ageSeconds_ = 0.0F;
    particleAgeSeconds_ = 0.0F;
    drumPhase_ = 0.0F;
    stringDelay_.fill(0.0F);
    stringDelayIndex_ = 0;
    lowPassGateState_ = 0.0F;
    noiseLowPassState_ = 0.0F;
    highPassPreviousInput_ = 0.0F;
    highPassState_ = 0.0F;
    return true;
}

void PlaitsVoice::noteOff() noexcept {
    active_ = false;
}

bool PlaitsVoice::render(float* monoOutput,
                         std::size_t frameCount,
                         std::uint32_t sampleRate) noexcept {
    if (monoOutput == nullptr ||
        frameCount > SynthVoiceRack::MaxRenderFrames ||
        sampleRate < 8000 || sampleRate > 192000 ||
        parameters_.pitchHz > static_cast<float>(sampleRate) * 0.5F) {
        return false;
    }

    if (!active_) {
        for (std::size_t i = 0; i < frameCount; ++i) {
            monoOutput[i] = 0.0F;
        }
        return true;
    }

    const auto engine = static_cast<EngineType>(currentEngine_);
    const float phaseStep = parameters_.pitchHz / static_cast<float>(sampleRate);
    const float secondsPerSample = 1.0F / static_cast<float>(sampleRate);
    const float gain = velocity_ * parameters_.level;
    const float timbre = parameters_.timbre;
    const float color = parameters_.color;
    const float morph = parameters_.morph;

    // One-pole "low-pass gate" colouring shared by every model.
    const float gateCoefficient = std::clamp(
        (200.0F + color * 12000.0F) * 2.0F / static_cast<float>(sampleRate),
        0.0F, 1.0F);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const float age = ageSeconds_;
        float sample = 0.0F;

        switch (engine) {
        case EngineType::VirtualAnalog: {
            const float pulseWidth = 0.15F + timbre * 0.7F;
            const float sine = fastSine(enginePhase_);
            const float triangle = 1.0F - 4.0F * std::abs(enginePhase_ - 0.5F);
            const float saw = enginePhase_ * 2.0F - 1.0F;
            const float pulse = enginePhase_ < pulseWidth ? 1.0F : -1.0F;
            const float position = morph * 3.0F;
            if (position < 1.0F) {
                sample = sine * (1.0F - position) + triangle * position;
            } else if (position < 2.0F) {
                sample = triangle * (2.0F - position) + saw * (position - 1.0F);
            } else {
                sample = saw * (3.0F - position) + pulse * (position - 2.0F);
            }
            break;
        }

        case EngineType::WaveTable: {
            const float brightness = 0.15F + timbre * 0.85F;
            sample = fastSine(enginePhase_);
            sample += brightness * 0.50F * fastSine(enginePhase_ * 2.0F);
            sample += brightness * 0.33F * fastSine(enginePhase_ * 3.0F);
            sample += brightness * 0.25F * fastSine(enginePhase_ * 4.0F);
            sample *= 0.62F;
            break;
        }

        case EngineType::FM: {
            const float ratio = 0.5F + timbre * 3.5F;
            const float index = color * 6.0F;
            const float modulator = fastSine(enginePhase_ * ratio) * index;
            sample = fastSine(enginePhase_ + modulator);
            break;
        }

        case EngineType::Additive: {
            const int harmonics = 1 + static_cast<int>(timbre * 7.0F);
            float sum = 0.0F;
            float normalizer = 0.0F;
            for (int h = 1; h <= harmonics; ++h) {
                const float amplitude = 1.0F / static_cast<float>(h);
                sum += fastSine(enginePhase_ * static_cast<float>(h)) *
                       amplitude;
                normalizer += amplitude;
            }
            sample = normalizer > 0.0F ? sum / normalizer : 0.0F;
            break;
        }

        case EngineType::Chord: {
            static constexpr float Ratios[5] = {0.5F, 1.0F, 1.5F, 2.0F, 3.0F};
            const float detune = 1.0F + morph * 0.01F;
            float sum = 0.0F;
            for (float ratio : Ratios) {
                sum += fastSine(enginePhase_ * ratio * detune);
            }
            sample = sum * 0.22F;
            break;
        }

        case EngineType::Speech: {
            const float base =
                std::max(1.0F, parameters_.pitchHz);
            const float formant1 =
                std::clamp((400.0F + timbre * 900.0F) / base, 1.0F, 8.0F);
            const float formant2 =
                std::clamp((900.0F + color * 1800.0F) / base, 1.0F, 12.0F);
            const float glottal = enginePhase_ < 0.4F ? 1.0F : -1.0F;
            sample = glottal *
                     (0.55F * fastSine(enginePhase_ * formant1) +
                      0.45F * fastSine(enginePhase_ * formant2));
            break;
        }

        case EngineType::BassDrum: {
            const float pitchEnvelope = fastExp(-age * 18.0F);
            const float sweptPitch =
                parameters_.pitchHz * (1.0F + pitchEnvelope * 3.0F);
            drumPhase_ += sweptPitch * secondsPerSample;
            drumPhase_ -= std::floor(drumPhase_);
            const float body =
                fastSine(drumPhase_) * fastExp(-age * 8.0F);
            const float click = noiseFrom(drumNoise_) * fastExp(-age * 60.0F);
            sample = body * 0.9F + click * 0.25F;
            break;
        }

        case EngineType::SnareDrum: {
            const float noise = noiseFrom(snareNoise_);
            const float tone =
                fastSine(enginePhase_ * 2.0F) * fastExp(-age * 26.0F);
            const float noiseEnvelope = fastExp(-age * 13.0F);
            sample = tone * 0.35F + noise * noiseEnvelope * 0.65F;
            break;
        }

        case EngineType::HiHat: {
            const float noise = noiseFrom(hiHatNoise_);
            highPassState_ =
                noise - highPassPreviousInput_ + 0.92F * highPassState_;
            highPassPreviousInput_ = noise;
            const float envelope =
                fastExp(-age * (40.0F + timbre * 90.0F));
            sample = highPassState_ * envelope * 0.8F;
            break;
        }

        case EngineType::Grain: {
            const float grainRate =
                20.0F + timbre * 180.0F;
            const float grainPhase =
                (enginePhase_ * grainRate) / parameters_.pitchHz;
            const float window = 0.5F - 0.5F * fastSine(grainPhase);
            sample = fastSine(enginePhase_) * window;
            break;
        }

        case EngineType::String: {
            const auto delayLength = static_cast<std::size_t>(std::clamp(
                static_cast<float>(sampleRate) / parameters_.pitchHz, 2.0F,
                static_cast<float>(stringDelay_.size())));
            if (stringDelayIndex_ >= delayLength) {
                stringDelayIndex_ = 0;
            }
            const float delayed = stringDelay_[stringDelayIndex_];
            const float feedback = 0.90F + color * 0.09F;
            const float excitation =
                fastSine(enginePhase_) * 0.18F + noiseFrom(drumNoise_) * 0.02F;
            stringDelay_[stringDelayIndex_] = delayed * feedback + excitation;
            if (++stringDelayIndex_ >= delayLength) {
                stringDelayIndex_ = 0;
            }
            sample = delayed * 0.8F;
            break;
        }

        case EngineType::Modal: {
            float sum = 0.0F;
            for (int mode = 1; mode <= 6; ++mode) {
                const float ratio =
                    1.0F + morph * 0.5F * static_cast<float>(mode);
                const float decay =
                    fastExp(-age * (6.0F + static_cast<float>(mode) * 3.0F));
                sum += fastSine(enginePhase_ * ratio) * decay /
                       static_cast<float>(mode);
            }
            sample = sum * 0.45F;
            break;
        }

        case EngineType::Noise: {
            float noise = noiseFrom(coloredNoise_);
            const float coefficient = 0.05F + timbre * 0.85F;
            noiseLowPassState_ +=
                coefficient * (noise - noiseLowPassState_);
            sample = noiseLowPassState_ * fastExp(-age * 4.0F) * 1.6F;
            break;
        }

        case EngineType::Particle: {
            const float probability =
                (2.0F + timbre * 40.0F) * secondsPerSample;
            particleAgeSeconds_ += secondsPerSample;
            if (noiseFrom(particleNoise_) * 0.5F + 0.5F < probability) {
                particleAgeSeconds_ = 0.0F;
                enginePhase_ = 0.0F;
            }
            sample = fastSine(enginePhase_) *
                     fastExp(-particleAgeSeconds_ * 22.0F);
            break;
        }

        case EngineType::PhaseDistortion: {
            const float amount = (0.2F + timbre * 0.8F) * 0.5F;
            const float distorted =
                enginePhase_ + amount * fastSine(enginePhase_ * 2.0F);
            sample = fastSine(distorted);
            break;
        }

        case EngineType::WaveShaping: {
            const float drive = 1.0F + color * 12.0F;
            sample = std::tanh(fastSine(enginePhase_) * drive) /
                     std::tanh(drive);
            break;
        }
        }

        // Low-pass gate colour. A coefficient of 1 at high colour keeps the
        // model open; darker settings round off the harmonics.
        lowPassGateState_ += gateCoefficient * (sample - lowPassGateState_);
        sample = lowPassGateState_;
        sample *= gain;

        monoOutput[frame] = std::clamp(sample, -1.0F, 1.0F);

        enginePhase_ += phaseStep;
        if (enginePhase_ >= 1.0F) {
            enginePhase_ -= 1.0F;
        }
        ageSeconds_ += secondsPerSample;
    }

    return true;
}

} // namespace pocket::audio
