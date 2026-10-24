#include "audio/SynthVoice.h"

#include <cmath>

namespace pocket::audio {

bool SynthVoiceRack::configure(std::size_t track,
                               ISynthVoice* voice,
                               const SynthParameters& parameters) noexcept {
    if (track >= voices_.size() || voice == nullptr ||
        !isValid(parameters) || !voice->isRealtimeSafe()) {
        return false;
    }
    for (std::size_t other = 0; other < voices_.size(); ++other) {
        if (other != track && voices_[other] == voice) {
            return false;
        }
    }
    if (!voice->setParameters(parameters)) {
        return false;
    }
    voices_[track] = voice;
    parameters_[track] = parameters;
    return true;
}

bool SynthVoiceRack::updateParameters(
    std::size_t track, const SynthParameters& parameters) noexcept {
    if (track >= voices_.size() || voices_[track] == nullptr ||
        !isValid(parameters)) {
        return false;
    }
    if (!voices_[track]->setParameters(parameters)) {
        return false;
    }
    parameters_[track] = parameters;
    return true;
}

const SynthParameters& SynthVoiceRack::parameters(
    std::size_t track) const noexcept {
    static constexpr SynthParameters DefaultParameters{};
    if (track >= parameters_.size()) {
        return DefaultParameters;
    }
    return parameters_[track];
}

bool SynthVoiceRack::trigger(std::size_t track, float velocity) noexcept {
    if (track >= voices_.size() || voices_[track] == nullptr ||
        !std::isfinite(velocity) || velocity < 0.0F || velocity > 1.0F) {
        return false;
    }
    return voices_[track]->noteOn(velocity);
}

bool SynthVoiceRack::renderTrack(std::size_t track,
                                 float* monoOutput,
                                 std::size_t frameCount,
                                 std::uint32_t sampleRate) noexcept {
    if (track >= voices_.size() || voices_[track] == nullptr ||
        monoOutput == nullptr || frameCount > MaxRenderFrames ||
        sampleRate < 8000 || sampleRate > 192000) {
        return false;
    }
    return voices_[track]->render(monoOutput, frameCount, sampleRate);
}

void SynthVoiceRack::stopTrack(std::size_t track) noexcept {
    if (track < voices_.size() && voices_[track] != nullptr) {
        voices_[track]->noteOff();
    }
}

void SynthVoiceRack::stopAll() noexcept {
    for (auto* voice : voices_) {
        if (voice != nullptr) {
            voice->noteOff();
        }
    }
}

bool SynthVoiceRack::isConfigured(std::size_t track) const noexcept {
    return track < voices_.size() && voices_[track] != nullptr;
}

bool SynthVoiceRack::isTrackRealtimeSafe(std::size_t track) const noexcept {
    return isConfigured(track) && voices_[track]->isRealtimeSafe();
}

bool SynthVoiceRack::isRealtimeSafe() const noexcept {
    for (const auto* voice : voices_) {
        if (voice != nullptr && !voice->isRealtimeSafe()) {
            return false;
        }
    }
    return true;
}

bool SynthVoiceRack::isValid(
    const SynthParameters& parameters) noexcept {
    return std::isfinite(parameters.pitchHz) &&
           parameters.pitchHz > 0.0F &&
           parameters.pitchHz <= MaxPitchHz &&
           std::isfinite(parameters.timbre) &&
           parameters.timbre >= 0.0F && parameters.timbre <= 1.0F &&
           std::isfinite(parameters.color) &&
           parameters.color >= 0.0F && parameters.color <= 1.0F &&
           std::isfinite(parameters.morph) &&
           parameters.morph >= 0.0F && parameters.morph <= 1.0F &&
           std::isfinite(parameters.level) &&
           parameters.level >= 0.0F && parameters.level <= 1.0F;
}

} // namespace pocket::audio
