#include "audio/SamplerMixerRenderer.h"

#include <algorithm>
#include <cmath>

namespace pocket::audio {
namespace {

// Synth tracks are mono, so panning alone leaves them dead centre. This
// constant amount of allpass-based side signal opens the image without
// touching sampler tracks, which stay punchy and centred.
constexpr float SynthStereoSpread = 0.30F;

} // namespace

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
        synthSettings_[track] = state.synth;
    }
    setEffectState(effectState_);
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
        synthSettings_[track] = state.synth;
        syncSynthVoice(track);
    }
    setEffectState(effectState_);
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
    effectState_ = state;
    delayEffect_.setParameters(effectState_);
    project::ProjectEffectState sendState = effectState_;
    sendState.delayMix = 1.0F;
    delaySendEffect_.setParameters(sendState);
}

const TrackSynthSettings& SamplerMixerRenderer::synthSettings(
    std::size_t track) const noexcept {
    static constexpr TrackSynthSettings DefaultSynth{};
    if (track >= synthSettings_.size()) {
        return DefaultSynth;
    }
    return synthSettings_[track];
}

bool SamplerMixerRenderer::syncSynthVoice(std::size_t track) noexcept {
    if (synthVoices_ == nullptr || !synthVoices_->isConfigured(track)) {
        return true;
    }
    SynthParameters params = synthVoices_->parameters(track);
    params.pitchHz = std::clamp(
        synthSettings_[track].pitchHz *
            std::pow(2.0F, pitchOffset_[track] / 12.0F),
        TrackSynthSettings::MinPitchHz, SynthVoiceRack::MaxPitchHz);
    params.morph = synthSettings_[track].morph;
    params.timbre = synthSettings_[track].timbre;
    params.color = synthSettings_[track].color;
    params.engine = synthSettings_[track].engineType;
    params.model = synthSettings_[track].model;
    return synthVoices_->updateParameters(track, params);
}

bool SamplerMixerRenderer::applySynthPitch(std::size_t track) noexcept {
    if (synthVoices_ == nullptr || !synthVoices_->isConfigured(track)) {
        return true;
    }
    SynthParameters params = synthVoices_->parameters(track);
    params.pitchHz = std::clamp(
        synthSettings_[track].pitchHz *
            std::pow(2.0F, pitchOffset_[track] / 12.0F),
        TrackSynthSettings::MinPitchHz, SynthVoiceRack::MaxPitchHz);
    return synthVoices_->updateParameters(track, params);
}

float SamplerMixerRenderer::playbackRateFor(std::size_t track) const noexcept {
    return std::clamp(std::pow(2.0F, pitchOffset_[track] / 12.0F),
                      SamplerVoicePool::MinPlaybackRate,
                      SamplerVoicePool::MaxPlaybackRate);
}

void SamplerMixerRenderer::updatePitchSmoothing(
    std::uint32_t frameCount, std::uint32_t sampleRate) noexcept {
    const float blockSeconds =
        static_cast<float>(frameCount) / static_cast<float>(sampleRate);
    for (std::size_t track = 0; track < TrackCount; ++track) {
        const auto& turing = sequencer_.track(track).turing;
        if (turing.glide == 0) {
            pitchOffset_[track] = targetPitchOffset_[track];
            continue;
        }
        const float glideSeconds =
            static_cast<float>(turing.glide) / 100.0F * 0.5F;
        const float coefficient =
            1.0F - std::exp(-blockSeconds / glideSeconds);
        pitchOffset_[track] +=
            (targetPitchOffset_[track] - pitchOffset_[track]) * coefficient;
    }
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
        if (state.instrument == InstrumentType::Midi) {
            // MIDI tracks are rendered by the MIDI output path, not by the
            // internal voices or envelope.
            continue;
        }
        targetPitchOffset_[track] =
            static_cast<float>(tick.pitchOffsets[track]);
        if (state.turing.glide == 0) {
            pitchOffset_[track] = targetPitchOffset_[track];
        }
        if (state.instrument == InstrumentType::Sampler) {
            if (!voices_.trigger(track, playbackRateFor(track))) {
                continue;
            }
        } else {
            if (synthVoices_ == nullptr || !applySynthPitch(track) ||
                !synthVoices_->trigger(track)) {
                return false;
            }
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
    if (command.parameter == AudioParameter::DelayTimeMs) {
        effectState_.delayMilliseconds =
            static_cast<std::uint16_t>(command.value);
        delayEffect_.setParameters(effectState_);
        project::ProjectEffectState sendState = effectState_;
        sendState.delayMix = 1.0F;
        delaySendEffect_.setParameters(sendState);
        return true;
    }
    if (command.parameter == AudioParameter::DelayFeedback) {
        effectState_.delayFeedback = command.value;
        delayEffect_.setParameters(effectState_);
        project::ProjectEffectState sendState = effectState_;
        sendState.delayMix = 1.0F;
        delaySendEffect_.setParameters(sendState);
        return true;
    }
    if (command.parameter == AudioParameter::DelayMix) {
        effectState_.delayMix = command.value;
        delayEffect_.setParameters(effectState_);
        return true;
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
    case AudioParameter::TrackReverbSend:
        mixerSettings.reverbSend = command.value;
        break;
    case AudioParameter::TrackDelaySend:
        mixerSettings.delaySend = command.value;
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
    case AudioParameter::TrackSynthPitch:
        synthSettings_[track].pitchHz = command.value;
        return syncSynthVoice(track);
    case AudioParameter::TrackSynthMorph:
        synthSettings_[track].morph = command.value;
        return syncSynthVoice(track);
    case AudioParameter::TrackSynthTimbre:
        synthSettings_[track].timbre = command.value;
        return syncSynthVoice(track);
    case AudioParameter::TrackSynthColor:
        synthSettings_[track].color = command.value;
        return syncSynthVoice(track);
    case AudioParameter::TrackSynthEngineType:
        synthSettings_[track].engineType = static_cast<SynthEngineType>(static_cast<int>(command.value));
        return syncSynthVoice(track);
    case AudioParameter::TrackSynthModel:
        synthSettings_[track].model =
            static_cast<std::uint8_t>(command.value);
        return syncSynthVoice(track);
    case AudioParameter::DelayTimeMs:
    case AudioParameter::DelayFeedback:
    case AudioParameter::DelayMix:
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

    // Sample swaps arrive from the UI thread through the voice pool's bounded
    // queue and are applied here, before any voice renders this segment.
    voices_.applyPendingSamples();
    updatePitchSmoothing(frameCount, sampleRate);

    for (std::size_t track = 0; track < TrackCount; ++track) {
        if (sequencer_.track(track).instrument == InstrumentType::Midi) {
            envelopes_[track].reset();
            voices_.stopTrack(track);
            if (synthVoices_ != nullptr) {
                synthVoices_->stopTrack(track);
            }
            for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
                trackBuffers_[track][frame] = 0.0F;
            }
            inputs_[track] = {trackBuffers_[track].data(), nullptr};
            continue;
        }
        if (!envelopes_[track].isActive()) {
            for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
                trackBuffers_[track][frame] = 0.0F;
            }
            inputs_[track] = {trackBuffers_[track].data(), nullptr};
            continue;
        }

        bool rendered = false;
        if (sequencer_.track(track).instrument == InstrumentType::Synth) {
            rendered = synthVoices_ != nullptr && applySynthPitch(track) &&
                       synthVoices_->renderTrack(
                           track, trackBuffers_[track].data(), frameCount,
                           sampleRate);
        } else {
            rendered = voices_.setTrackPlaybackRate(
                           track, playbackRateFor(track)) &&
                       voices_.renderTrack(track, trackBuffers_[track].data(),
                                           frameCount, sampleRate);
        }
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
    const SendBuffers sends{reverbSendBuffer_.data(), delaySendBuffer_.data()};
    for (std::size_t track = 0; track < TrackCount; ++track) {
        settings_[track].spread =
            sequencer_.track(track).instrument == InstrumentType::Synth
                ? SynthStereoSpread
                : 0.0F;
    }
    if (!mixer_.processBlock(inputs_, settings_, masterVolumePercent_,
                             sampleRate, output, frameCount, &sends)) {
        return false;
    }

    // Effect sends are processed 100% wet and summed with the dry master.
    reverbEffect_.processSendStereo(reverbSendBuffer_.data(), frameCount,
                                    sampleRate);
    delaySendEffect_.processStereo(delaySendBuffer_.data(), frameCount,
                                   sampleRate);

    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        const auto outputFrame = outputOffset + frame;
        interleavedStereo[outputFrame * 2] =
            outputLeft_[frame] + reverbSendBuffer_[frame * 2] +
            delaySendBuffer_[frame * 2];
        interleavedStereo[outputFrame * 2 + 1] =
            outputRight_[frame] + reverbSendBuffer_[frame * 2 + 1] +
            delaySendBuffer_[frame * 2 + 1];
    }
    delayEffect_.processStereo(
        interleavedStereo + outputOffset * 2, frameCount, sampleRate);
    // Fixed master tone and width, then the final safety limiter. This runs
    // last so the dry mix, reverb, and delay all share one voicing.
    if (!masterBus_.prepare(sampleRate)) {
        return false;
    }
    masterBus_.process(interleavedStereo + outputOffset * 2, frameCount);
    return true;
}

} // namespace pocket::audio
