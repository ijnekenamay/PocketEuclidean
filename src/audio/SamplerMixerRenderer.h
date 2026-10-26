#pragma once

#include "audio/AdEnvelope.h"
#include "audio/DelayEffect.h"
#include "audio/AudioParameterQueue.h"
#include "audio/Mixer.h"
#include "audio/OnePoleLowPass.h"
#include "audio/ReverbEffect.h"
#include "audio/SamplerVoicePool.h"
#include "audio/SynthVoice.h"
#include "sequencer/Sequencer.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket::audio {

class SamplerMixerRenderer {
public:
    static constexpr std::size_t MaxRenderFrames = 4096;

    SamplerMixerRenderer(Sequencer& sequencer,
                         SamplerVoicePool& voices,
                         Mixer& mixer,
                         std::uint8_t masterVolumePercent = 100) noexcept;
    SamplerMixerRenderer(Sequencer& sequencer,
                         SamplerVoicePool& samplerVoices,
                         SynthVoiceRack& synthVoices,
                         Mixer& mixer,
                         std::uint8_t masterVolumePercent = 100) noexcept;

    // Setup-only; do not call while the audio callback is rendering.
    bool setMasterVolume(std::uint8_t volumePercent) noexcept;
    void setEffectState(const project::ProjectEffectState& state) noexcept;
    const DelayEffect& delayEffect() const noexcept { return delayEffect_; }
    const TrackSynthSettings& synthSettings(std::size_t track) const noexcept;
    bool isRealtimeSafe() const noexcept;
    bool trigger(const TickResult& tick, std::uint32_t sampleRate) noexcept;
    bool applyCommand(const AudioParameterCommand& command,
                      std::uint32_t sampleRate) noexcept;

    bool render(float* interleavedStereo,
                std::uint32_t outputOffset,
                std::uint32_t frameCount,
                std::uint32_t sampleRate) noexcept;

private:
    bool syncSynthVoice(std::size_t track) noexcept;

    Sequencer& sequencer_;
    SamplerVoicePool& voices_;
    SynthVoiceRack* synthVoices_ = nullptr;
    Mixer& mixer_;
    std::uint8_t masterVolumePercent_;
    std::array<std::array<float, MaxRenderFrames>, TrackCount> trackBuffers_{};
    std::array<AdEnvelope, TrackCount> envelopes_{};
    std::array<OnePoleLowPass, TrackCount> filters_{};
    std::array<TrackFilterSettings, TrackCount> lastFilterSettings_{};
    std::array<std::uint32_t, TrackCount> filterSampleRates_{};
    std::array<AdEnvelopeSettings, TrackCount> envelopeSettings_{};
    std::array<TrackFilterSettings, TrackCount> filterSettings_{};
    std::array<TrackSynthSettings, TrackCount> synthSettings_{};
    std::array<float, MaxRenderFrames> outputLeft_{};
    std::array<float, MaxRenderFrames> outputRight_{};
    std::array<StereoInputBuffer, TrackCount> inputs_{};
    std::array<TrackMixerSettings, TrackCount> settings_{};
    std::array<float, MaxRenderFrames * 2> reverbSendBuffer_{};
    std::array<float, MaxRenderFrames * 2> delaySendBuffer_{};
    project::ProjectEffectState effectState_{};
    DelayEffect delayEffect_{};
    DelayEffect delaySendEffect_{};
    ReverbEffect reverbEffect_{};
};

} // namespace pocket::audio
