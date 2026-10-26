#pragma once

#include "audio/AudioParameterQueue.h"
#include "audio/SequencerCommandQueue.h"
#include "sequencer/Sequencer.h"
#include "ui/ModulationMirror.h"
#include "ui/StepTriggerEventQueue.h"
#include "ui/PlayheadMirror.h"

#include <atomic>
#include <cstdint>

namespace pocket::audio {

class SamplerMixerRenderer;

class AudioCallbackProcessor {
public:
    static constexpr std::uint32_t MaxCallbackFrames = 4096;

    using ScheduledTickCallback = void (*)(void* userData,
                                            std::uint32_t sampleOffset,
                                            const TickResult& tick) noexcept;

    AudioCallbackProcessor(
        Sequencer& sequencer,
        ui::StepTriggerEventQueue& visualEvents,
        ScheduledTickCallback tickCallback = nullptr,
        void* tickUserData = nullptr) noexcept;
    AudioCallbackProcessor(
        Sequencer& sequencer,
        ui::StepTriggerEventQueue& visualEvents,
        SamplerMixerRenderer& renderer,
        ScheduledTickCallback tickCallback = nullptr,
        void* tickUserData = nullptr) noexcept;

    bool process(float* interleavedStereo,
                 std::uint32_t frameCount,
                 std::uint64_t blockStartFrame,
                 std::uint32_t sampleRate) noexcept;

    bool isRealtimeSafe() const noexcept;
    // Setup-only; connect one UI producer before opening the audio device.
    void setParameterQueue(AudioParameterQueue* queue) noexcept {
        parameterQueue_ = queue;
    }
    // Setup-only. The audio thread then owns the Sequencer and applies edits
    // from this queue at the start of each block.
    void setSequencerCommandQueue(SequencerCommandQueue* queue) noexcept {
        sequencerQueue_ = queue;
    }
    void setPlayheadMirror(ui::PlayheadMirror* mirror) noexcept {
        playhead_ = mirror;
    }
    // Optional. Publishes the modulated rhythm and LFO phase for the UI once
    // per tick; without it the UI keeps showing base values while playing.
    void setModulationMirror(ui::ModulationMirror* mirror) noexcept {
        modulation_ = mirror;
    }
    void resetTimeline() noexcept;
    std::uint32_t callbackFailureCount() const noexcept {
        return callbackFailureCount_.load(std::memory_order_relaxed);
    }

    static void callback(void* userData,
                         float* interleavedStereo,
                         std::uint32_t frameCount,
                         std::uint64_t blockStartFrame,
                         std::uint32_t sampleRate) noexcept;

private:
    Sequencer& sequencer_;
    ui::StepTriggerEventQueue& visualEvents_;
    ScheduledTickCallback tickCallback_;
    void* tickUserData_;
    SamplerMixerRenderer* renderer_ = nullptr;
    AudioParameterQueue* parameterQueue_ = nullptr;
    SequencerCommandQueue* sequencerQueue_ = nullptr;
    ui::PlayheadMirror* playhead_ = nullptr;
    ui::ModulationMirror* modulation_ = nullptr;
    std::uint64_t nextBlockStartFrame_ = 0;
    std::atomic<std::uint32_t> callbackFailureCount_{0};
};

} // namespace pocket::audio
