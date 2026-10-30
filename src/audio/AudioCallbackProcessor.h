#pragma once

#include "audio/AudioParameterQueue.h"
#include "audio/SequencerCommandQueue.h"
#include "platform/BackendInterfaces.h"
#include "sequencer/MidiClock.h"
#include "sequencer/Sequencer.h"
#include "ui/ModulationMirror.h"
#include "ui/StepTriggerEventQueue.h"
#include "ui/PlayheadMirror.h"

#include <array>
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
    // Setup-only. MIDI output is driven from the audio thread so clock and
    // note timing do not depend on the UI frame rate.
    void setMidiBackend(platform::IMidiBackend* backend) noexcept {
        midiBackend_ = backend;
    }
    void setMidiOutputEnabled(bool enabled) noexcept {
        midiOutputEnabled_.store(enabled, std::memory_order_relaxed);
    }
    // 1..16. Track N is sent on the next channel, wrapping at 16.
    void setMidiBaseChannel(std::uint8_t channel) noexcept {
        midiBaseChannel_.store(channel >= 1 && channel <= 16 ? channel - 1
                                                             : 0,
                               std::memory_order_relaxed);
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
    void sendMidi(platform::MidiEvent::Type type,
                  std::uint8_t channel,
                  std::uint8_t data1 = 0,
                  std::uint8_t data2 = 0) noexcept;
    void stopMidiNotes() noexcept;
    void processMidiOutput(std::uint32_t frameCount,
                           std::uint32_t sampleRate,
                           std::uint64_t blockStartFrame) noexcept;
    void startMidiNote(std::size_t track,
                       const TickResult& tick,
                       std::uint32_t sampleOffset,
                       std::uint64_t blockStartFrame,
                       std::uint32_t sampleRate) noexcept;
    static void clockPulse(void* userData,
                           std::uint32_t sampleOffset) noexcept;

    std::uint8_t midiChannel(std::size_t track) const noexcept {
        const auto base = midiBaseChannel_.load(std::memory_order_relaxed);
        return static_cast<std::uint8_t>(((base + track) % 16) + 1);
    }

    Sequencer& sequencer_;
    ui::StepTriggerEventQueue& visualEvents_;
    ScheduledTickCallback tickCallback_;
    void* tickUserData_;
    SamplerMixerRenderer* renderer_ = nullptr;
    AudioParameterQueue* parameterQueue_ = nullptr;
    SequencerCommandQueue* sequencerQueue_ = nullptr;
    ui::PlayheadMirror* playhead_ = nullptr;
    ui::ModulationMirror* modulation_ = nullptr;
    platform::IMidiBackend* midiBackend_ = nullptr;
    std::atomic<bool> midiOutputEnabled_{false};
    std::atomic<std::uint8_t> midiBaseChannel_{0};
    MidiClockGenerator midiClockGenerator_;
    std::uint32_t lastTransportRevision_ = 0;
    bool midiWasEnabled_ = false;
    std::array<bool, TrackCount> midiNoteActive_{};
    std::array<std::uint8_t, TrackCount> midiNoteNumber_{};
    std::array<std::uint64_t, TrackCount> midiNoteOffFrame_{};
    std::uint64_t nextBlockStartFrame_ = 0;
    std::atomic<std::uint32_t> callbackFailureCount_{0};
};

} // namespace pocket::audio
