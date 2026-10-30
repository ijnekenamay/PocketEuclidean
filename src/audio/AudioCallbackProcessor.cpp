#include "audio/AudioCallbackProcessor.h"
#include "audio/SamplerMixerRenderer.h"
#include "sequencer/MidiNote.h"

#include <algorithm>
#include <limits>

namespace pocket::audio {

AudioCallbackProcessor::AudioCallbackProcessor(
    Sequencer& sequencer,
    ui::StepTriggerEventQueue& visualEvents,
    ScheduledTickCallback tickCallback,
    void* tickUserData) noexcept
    : sequencer_(sequencer),
      visualEvents_(visualEvents),
      tickCallback_(tickCallback),
      tickUserData_(tickUserData) {}

AudioCallbackProcessor::AudioCallbackProcessor(
    Sequencer& sequencer,
    ui::StepTriggerEventQueue& visualEvents,
    SamplerMixerRenderer& renderer,
    ScheduledTickCallback tickCallback,
    void* tickUserData) noexcept
    : sequencer_(sequencer),
      visualEvents_(visualEvents),
      tickCallback_(tickCallback),
      tickUserData_(tickUserData),
      renderer_(&renderer) {}

bool AudioCallbackProcessor::isRealtimeSafe() const noexcept {
    return visualEvents_.isLockFree() && callbackFailureCount_.is_lock_free() &&
           midiOutputEnabled_.is_lock_free() &&
           midiBaseChannel_.is_lock_free() &&
           (sequencerQueue_ == nullptr || sequencerQueue_->isLockFree()) &&
           (renderer_ == nullptr || renderer_->isRealtimeSafe()) &&
           (playhead_ == nullptr || playhead_->isLockFree()) &&
           (modulation_ == nullptr || modulation_->isLockFree()) &&
           (parameterQueue_ == nullptr ||
            (renderer_ != nullptr && parameterQueue_->isLockFree()));
}

void AudioCallbackProcessor::resetTimeline() noexcept {
    nextBlockStartFrame_ = 0;
    visualEvents_.reset();
    callbackFailureCount_.store(0, std::memory_order_relaxed);
    midiClockGenerator_.reset();
    midiWasEnabled_ = false;
    lastTransportRevision_ = sequencer_.transportRevision();
    midiNoteActive_.fill(false);
    midiNoteNumber_.fill(0);
    midiNoteOffFrame_.fill(0);
}

void AudioCallbackProcessor::sendMidi(platform::MidiEvent::Type type,
                                      std::uint8_t channel,
                                      std::uint8_t data1,
                                      std::uint8_t data2) noexcept {
    if (midiBackend_ == nullptr) {
        return;
    }
    platform::MidiEvent event;
    event.type = type;
    event.channel = channel;
    event.data1 = data1;
    event.data2 = data2;
    event.timestampMicros = 0;
    (void)midiBackend_->send(event);
}

void AudioCallbackProcessor::stopMidiNotes() noexcept {
    for (std::size_t track = 0; track < TrackCount; ++track) {
        if (!midiNoteActive_[track]) {
            continue;
        }
        sendMidi(platform::MidiEvent::Type::NoteOff, midiChannel(track),
                 midiNoteNumber_[track]);
        midiNoteActive_[track] = false;
    }
}

void AudioCallbackProcessor::clockPulse(void* userData,
                                        std::uint32_t) noexcept {
    auto* self = static_cast<AudioCallbackProcessor*>(userData);
    self->sendMidi(platform::MidiEvent::Type::Clock, 1);
}

void AudioCallbackProcessor::processMidiOutput(
    std::uint32_t frameCount,
    std::uint32_t sampleRate,
    std::uint64_t blockStartFrame) noexcept {
    const bool enabled =
        midiBackend_ != nullptr &&
        midiOutputEnabled_.load(std::memory_order_relaxed);
    if (!enabled) {
        if (midiWasEnabled_) {
            stopMidiNotes();
            midiClockGenerator_.reset();
            midiWasEnabled_ = false;
        }
        return;
    }
    if (!midiWasEnabled_) {
        midiWasEnabled_ = true;
        midiClockGenerator_.reset();
        lastTransportRevision_ = sequencer_.transportRevision();
        midiNoteActive_.fill(false);
    }

    // Transport messages follow the audio-side Sequencer revision so a
    // Start/Continue/Stop is never lost between UI frames.
    const auto revision = sequencer_.transportRevision();
    if (revision != lastTransportRevision_) {
        lastTransportRevision_ = revision;
        if (!sequencer_.isPlaying()) {
            stopMidiNotes();
            sendMidi(platform::MidiEvent::Type::Stop, 1);
        } else if (sequencer_.lastStartWasResume()) {
            sendMidi(platform::MidiEvent::Type::Continue, 1);
        } else {
            sendMidi(platform::MidiEvent::Type::Start, 1);
        }
    }

    // Clock is deliberately independent of transport: F8 keeps running while
    // the sequencer is stopped, at the current master tempo.
    midiClockGenerator_.process(frameCount, sampleRate, sequencer_.tempo(),
                                &AudioCallbackProcessor::clockPulse, this);

    const bool playing = sequencer_.isPlaying();
    for (std::size_t track = 0; track < TrackCount; ++track) {
        if (!midiNoteActive_[track]) {
            continue;
        }
        const auto& state = sequencer_.track(track);
        if (!playing || state.instrument != InstrumentType::Midi ||
            state.muted || state.volume == 0 ||
            blockStartFrame >= midiNoteOffFrame_[track]) {
            sendMidi(platform::MidiEvent::Type::NoteOff, midiChannel(track),
                     midiNoteNumber_[track]);
            midiNoteActive_[track] = false;
        }
    }
}

void AudioCallbackProcessor::startMidiNote(
    std::size_t track,
    const TickResult& tick,
    std::uint32_t sampleOffset,
    std::uint64_t blockStartFrame,
    std::uint32_t sampleRate) noexcept {
    const auto& state = sequencer_.track(track);
    if (state.muted || state.volume == 0) {
        return;
    }
    if (midiNoteActive_[track]) {
        sendMidi(platform::MidiEvent::Type::NoteOff, midiChannel(track),
                 midiNoteNumber_[track]);
        midiNoteActive_[track] = false;
    }
    const auto note =
        midiNoteFromHz(state.synth.pitchHz, tick.pitchOffsets[track]);
    const auto velocity = midiVelocityFromVolume(state.volume);
    sendMidi(platform::MidiEvent::Type::NoteOn, midiChannel(track), note,
             velocity);
    midiNoteActive_[track] = true;
    midiNoteNumber_[track] = note;

    const double samplesPerStep =
        static_cast<double>(sampleRate) * 60.0 /
        (std::max(sequencer_.tempo(), 1.0) * 4.0);
    const auto gate =
        static_cast<std::uint64_t>(std::max(1.0, samplesPerStep * 0.5));
    midiNoteOffFrame_[track] = blockStartFrame + sampleOffset + gate;
}

bool AudioCallbackProcessor::process(float* interleavedStereo,
                                     std::uint32_t frameCount,
                                     std::uint64_t blockStartFrame,
                                     std::uint32_t sampleRate) noexcept {
    if (interleavedStereo == nullptr || frameCount > MaxCallbackFrames) {
        return false;
    }

    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        interleavedStereo[frame * 2] = 0.0F;
        interleavedStereo[frame * 2 + 1] = 0.0F;
    }

    if (sampleRate < 8000 || sampleRate > 192000 ||
        blockStartFrame >
            std::numeric_limits<std::uint64_t>::max() - frameCount ||
        blockStartFrame != nextBlockStartFrame_) {
        return false;
    }

    if (parameterQueue_ != nullptr) {
        if (renderer_ == nullptr) {
            return false;
        }
        AudioParameterCommand command;
        while (parameterQueue_->pop(command)) {
            if (!renderer_->applyCommand(command, sampleRate)) {
                return false;
            }
        }
    }

    if (sequencerQueue_ != nullptr) {
        SequencerCommand command;
        while (sequencerQueue_->pop(command)) {
            // A rejected edit must not stop later ones from being applied.
            (void)applySequencerCommand(sequencer_, command);
        }
    }

    processMidiOutput(frameCount, sampleRate, blockStartFrame);

    bool valid = true;
    std::uint32_t renderedFrames = 0;
    const bool scheduled = sequencer_.processFrames(
        frameCount, sampleRate,
        [this, blockStartFrame, frameCount, &valid,
         &renderedFrames, interleavedStereo,
         sampleRate](std::uint32_t sampleOffset,
                          const TickResult& tick) noexcept {
            if (!tick.valid) {
                valid = false;
                return;
            }
            if (sampleOffset > frameCount || sampleOffset < renderedFrames) {
                valid = false;
                return;
            }
            if (sampleOffset > std::numeric_limits<std::uint64_t>::max() -
                                   blockStartFrame) {
                valid = false;
                return;
            }

            if (renderer_ != nullptr &&
                !renderer_->render(interleavedStereo, renderedFrames,
                                   sampleOffset - renderedFrames, sampleRate)) {
                valid = false;
                return;
            }
            renderedFrames = sampleOffset;
            if (renderer_ != nullptr &&
                !renderer_->trigger(tick, sampleRate)) {
                valid = false;
                return;
            }
            if (midiBackend_ != nullptr &&
                midiOutputEnabled_.load(std::memory_order_relaxed)) {
                for (std::size_t t = 0; t < TrackCount; ++t) {
                    if (tick.triggered[t] &&
                        sequencer_.track(t).instrument ==
                            InstrumentType::Midi) {
                        startMidiNote(t, tick, sampleOffset, blockStartFrame,
                                      sampleRate);
                    }
                }
            }
            visualEvents_.pushTick(blockStartFrame + sampleOffset, tick);
            if (playhead_ != nullptr) {
                for (std::size_t t = 0; t < TrackCount; ++t) {
                    const auto& s = sequencer_.track(t);
                    playhead_->publish(t, s.position,
                                       s.effectiveRhythm.length);
                }
            }
            if (modulation_ != nullptr) {
                for (std::size_t t = 0; t < TrackCount; ++t) {
                    const auto& s = sequencer_.track(t);
                    const auto lfo = sequencer_.lfoDisplay(t);
                    modulation_->publish(t, s.effectiveRhythm,
                                         static_cast<float>(lfo.phase),
                                         static_cast<float>(lfo.value));
                    bool jitterValid = false;
                    const auto jitterEvents =
                        sequencer_.jitterEventsMask(t, jitterValid);
                    modulation_->publishJitter(t, jitterEvents, jitterValid);
                    modulation_->publishTuring(t,
                                               sequencer_.turingDisplay(t));
                }
            }
            if (tickCallback_ != nullptr) {
                tickCallback_(tickUserData_, sampleOffset, tick);
            }
        });
    if (renderer_ != nullptr &&
        !renderer_->render(interleavedStereo, renderedFrames,
                           frameCount - renderedFrames, sampleRate)) {
        valid = false;
    }
    nextBlockStartFrame_ = blockStartFrame + frameCount;
    return scheduled && valid;
}

void AudioCallbackProcessor::callback(void* userData,
                                      float* interleavedStereo,
                                      std::uint32_t frameCount,
                                      std::uint64_t blockStartFrame,
                                      std::uint32_t sampleRate) noexcept {
    if (userData == nullptr) {
        if (interleavedStereo != nullptr &&
            frameCount <= MaxCallbackFrames) {
            for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
                interleavedStereo[frame * 2] = 0.0F;
                interleavedStereo[frame * 2 + 1] = 0.0F;
            }
        }
        return;
    }
    auto& processor = *static_cast<AudioCallbackProcessor*>(userData);
    if (!processor.process(interleavedStereo, frameCount, blockStartFrame,
                           sampleRate)) {
        processor.callbackFailureCount_.fetch_add(
            1, std::memory_order_relaxed);
    }
}

} // namespace pocket::audio
