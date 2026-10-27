#include "audio/AudioCallbackProcessor.h"
#include "audio/SamplerMixerRenderer.h"

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
