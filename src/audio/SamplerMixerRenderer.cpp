#include "audio/SamplerMixerRenderer.h"

namespace pocket::audio {

SamplerMixerRenderer::SamplerMixerRenderer(
    Sequencer& sequencer,
    SamplerVoicePool& voices,
    Mixer& mixer,
    std::uint8_t masterVolumePercent) noexcept
    : sequencer_(sequencer),
      voices_(voices),
      mixer_(mixer),
      masterVolumePercent_(masterVolumePercent) {
    for (std::size_t track = 0; track < TrackCount; ++track) {
        const auto& state = sequencer_.track(track);
        settings_[track] = Mixer::settingsForTrack(state);
        envelopeSettings_[track] = state.envelope;
        filterSettings_[track] = state.filter;
    }
}

SamplerMixerRenderer::SamplerMixerRenderer(
    Sequencer& sequencer,
    SamplerVoicePool& samplerVoices,
    SynthVoiceRack& synthVoices,
    Mixer& mixer,
    std::uint8_t masterVolumePercent) noexcept
    : sequencer_(sequencer),
      voices_(samplerVoices),
      synthVoices_(&synthVoices),
      mixer_(mixer),
      masterVolumePercent_(masterVolumePercent) {
    for (std::size_t track = 0; track < TrackCount; ++track) {
        const auto& state = sequencer_.track(track);
        settings_[track] = Mixer::settingsForTrack(state);
        envelopeSettings_[track] = state.envelope;
        filterSettings_[track] = state.filter;
    }
}

bool SamplerMixerRenderer::setMasterVolume(
    std::uint8_t volumePercent) noexcept {
    if (volumePercent > 100) {
        return false;
    }
    masterVolumePercent_ = volumePercent;
    return true;
}

void SamplerMixerRenderer::setEffectState(
    const project::ProjectEffectState& state) noexcept {
    delayEffect_.setParameters(state);
}

bool SamplerMixerRenderer::isRealtimeSafe() const noexcept {
    if (!mixer_.isRealtimeSafe()) {
        return false;
    }
    for (std::size_t track = 0; track < TrackCount; ++track) {
        if (sequencer_.track(track).instrument == InstrumentType::Synth &&
            (synthVoices_ == nullptr ||
             !synthVoices_->isTrackRealtimeSafe(track))) {
            return false;
        }
    }
    return synthVoices_ == nullptr || synthVoices_->isRealtimeSafe();
}

bool SamplerMixerRenderer::trigger(const TickResult& tick,
                                   std::uint32_t sampleRate) noexcept {
    if (!tick.valid || sampleRate < 8000 || sampleRate > 192000) {
        return false;
    }
    for (std::size_t track = 0; track < TrackCount; ++track) {
        if (!tick.triggered[track]) {
            continue;
        }
        const auto& state = sequencer_.track(track);
        if (state.instrument == InstrumentType::Sampler) {
            if (!voices_.trigger(track)) {
                continue;
            }
        } else if (synthVoices_ == nullptr ||
                   !synthVoices_->trigger(track)) {
            return false;
        }
        if (!envelopes_[track].trigger(envelopeSettings_[track], sampleRate)) {
            return false;
        }
    }
    return true;
}

bool SamplerMixerRenderer::applyCommand(
    const AudioParameterCommand& command,
    std::uint32_t sampleRate) noexcept {
    if (!AudioParameterQueue::isValid(command) || sampleRate < 8000 ||
        sampleRate > 192000) {
        return false;
    }
    if (command.parameter == AudioParameter::MasterVolume) {
        return setMasterVolume(static_cast<std::uint8_t>(command.value));
    }

    const auto track = static_cast<std::size_t>(command.track);
    auto& mixerSettings = settings_[track];
    switch (command.parameter) {
    case AudioParameter::TrackVolume:
        mixerSettings.volumePercent = static_cast<std::uint8_t>(command.value);
        break;
    case AudioParameter::TrackPan:
        mixerSettings.pan = command.value;
        break;
    case AudioParameter::TrackMute:
        mixerSettings.muted = command.value == 1.0F;
        break;
    case AudioParameter::TrackAttack:
        envelopeSettings_[track].attackSeconds = command.value;
        if (envelopes_[track].isActive() &&
            !envelopes_[track].updateSettings(envelopeSettings_[track])) {
            return false;
        }
        break;
    case AudioParameter::TrackDecay:
        envelopeSettings_[track].decaySeconds = command.value;
        if (envelopes_[track].isActive() &&
            !envelopes_[track].updateSettings(envelopeSettings_[track])) {
            return false;
        }
        break;
    case AudioParameter::TrackFilterEnabled:
        filterSettings_[track].enabled = command.value == 1.0F;
        filterSampleRates_[track] = 0;
        break;
    case AudioParameter::TrackFilterCutoff:
        filterSettings_[track].cutoffHz = command.value;
        filterSampleRates_[track] = 0;
        break;
    case AudioParameter::MasterVolume:
        return false;
    }
    return true;
}

bool SamplerMixerRenderer::render(float* interleavedStereo,
                                  std::uint32_t outputOffset,
                                  std::uint32_t frameCount,
                                  std::uint32_t sampleRate) noexcept {
    if (interleavedStereo == nullptr || outputOffset > MaxRenderFrames ||
        frameCount > MaxRenderFrames - outputOffset ||
        sampleRate < 8000 || sampleRate > 192000 ||
        masterVolumePercent_ > 100) {
        return false;
    }

    for (std::size_t track = 0; track < TrackCount; ++track) {
        if (!envelopes_[track].isActive()) {
            for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
                trackBuffers_[track][frame] = 0.0F;
            }
            inputs_[track] = {trackBuffers_[track].data(), nullptr};
            continue;
        }

        const bool rendered =
            sequencer_.track(track).instrument == InstrumentType::Synth
                ? synthVoices_ != nullptr &&
                      synthVoices_->renderTrack(
                          track, trackBuffers_[track].data(), frameCount,
                          sampleRate)
                : voices_.renderTrack(track, trackBuffers_[track].data(),
                                      frameCount, sampleRate);
        if (!rendered) {
            return false;
        }
        const auto& filter = filterSettings_[track];
        if (filter.enabled) {
            if (filterSampleRates_[track] != sampleRate ||
                lastFilterSettings_[track].cutoffHz != filter.cutoffHz) {
                if (!filters_[track].setCutoff(filter.cutoffHz, sampleRate,
                                               filter.enabled)) {
                    return false;
                }
                lastFilterSettings_[track] = filter;
                filterSampleRates_[track] = sampleRate;
            }
            for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
                trackBuffers_[track][frame] =
                    filters_[track].process(trackBuffers_[track][frame]);
            }
        } else {
            if (filterSampleRates_[track] != sampleRate ||
                lastFilterSettings_[track].enabled != filter.enabled) {
                if (!filters_[track].setCutoff(filter.cutoffHz, sampleRate,
                                               false)) {
                    return false;
                }
                lastFilterSettings_[track] = filter;
                filterSampleRates_[track] = sampleRate;
            }
            for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
                trackBuffers_[track][frame] =
                    filters_[track].process(trackBuffers_[track][frame]);
            }
        }
        for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
            trackBuffers_[track][frame] =
                envelopes_[track].process(trackBuffers_[track][frame]);
        }
        if (!envelopes_[track].isActive()) {
            voices_.stopTrack(track);
            if (synthVoices_ != nullptr) {
                synthVoices_->stopTrack(track);
            }
            filters_[track].reset();
        }
        inputs_[track] = {trackBuffers_[track].data(), nullptr};
    }

    const StereoOutputBuffer output{outputLeft_.data(), outputRight_.data()};
    if (!mixer_.processBlock(inputs_, settings_, masterVolumePercent_,
                             sampleRate, output, frameCount)) {
        return false;
    }

    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        const auto outputFrame = outputOffset + frame;
        interleavedStereo[outputFrame * 2] = outputLeft_[frame];
        interleavedStereo[outputFrame * 2 + 1] = outputRight_[frame];
    }
    delayEffect_.processStereo(
        interleavedStereo + outputOffset * 2, frameCount, sampleRate);
    return true;
}

} // namespace pocket::audio
