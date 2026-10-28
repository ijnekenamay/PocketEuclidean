#include "audio/SamplerVoicePool.h"

#include <cmath>

namespace pocket::audio {

SamplerVoicePool::SamplerVoicePool() noexcept {
    for (auto& id : appliedSampleIds_) {
        id.store(0, std::memory_order_relaxed);
    }
}

bool SamplerVoicePool::assignSample(
    std::size_t track, const MonoSampleView& sample) noexcept {
    if (track >= samples_.size() || sample.samples == nullptr ||
        sample.frameCount == 0 || sample.sampleRate < 8000 ||
        sample.sampleRate > 192000) {
        return false;
    }
    for (std::size_t frame = 0; frame < sample.frameCount; ++frame) {
        if (!std::isfinite(sample.samples[frame])) {
            return false;
        }
    }

    stopTrack(track);
    samples_[track] = sample;
    return true;
}

bool SamplerVoicePool::queueSample(std::size_t track,
                                   const MonoSampleView& sample,
                                   std::uint32_t id) noexcept {
    if (track >= samples_.size() || sample.samples == nullptr ||
        sample.frameCount == 0 || sample.frameCount > MaxSampleFrames ||
        sample.sampleRate < 8000 || sample.sampleRate > 192000 || id == 0) {
        return false;
    }
    const auto write = sampleWriteIndex_.load(std::memory_order_relaxed);
    const auto read = sampleReadIndex_.load(std::memory_order_acquire);
    if (write - read >= SampleQueueCapacity) {
        return false;
    }
    sampleRequests_[write % SampleQueueCapacity] = {
        static_cast<std::uint8_t>(track), sample, id};
    sampleWriteIndex_.store(write + 1, std::memory_order_release);
    return true;
}

void SamplerVoicePool::applyPendingSamples() noexcept {
    auto read = sampleReadIndex_.load(std::memory_order_relaxed);
    const auto write = sampleWriteIndex_.load(std::memory_order_acquire);
    while (read != write) {
        const auto& request = sampleRequests_[read % SampleQueueCapacity];
        // The old buffer must not be referenced by any voice after this.
        stopTrack(request.track);
        samples_[request.track] = request.view;
        appliedSampleIds_[request.track].store(request.id,
                                               std::memory_order_release);
        ++read;
    }
    sampleReadIndex_.store(read, std::memory_order_release);
}

std::uint32_t SamplerVoicePool::appliedSampleId(
    std::size_t track) const noexcept {
    if (track >= appliedSampleIds_.size()) {
        return 0;
    }
    return appliedSampleIds_[track].load(std::memory_order_acquire);
}

MonoSampleView SamplerVoicePool::sample(std::size_t track) const noexcept {
    return track < samples_.size() ? samples_[track] : MonoSampleView{};
}

bool SamplerVoicePool::trigger(std::size_t track,
                               float playbackRate) noexcept {
    if (track >= samples_.size() || samples_[track].samples == nullptr ||
        !std::isfinite(playbackRate) || playbackRate < MinPlaybackRate ||
        playbackRate > MaxPlaybackRate) {
        return false;
    }

    Voice* selected = nullptr;
    for (auto& voice : voices_) {
        if (!voice.active) {
            selected = &voice;
            break;
        }
        if (selected == nullptr || voice.startOrder < selected->startOrder) {
            selected = &voice;
        }
    }
    if (selected == nullptr) {
        return false;
    }
    if (selected->active) {
        ++stolenVoiceCount_;
    }

    const auto& sample = samples_[track];
    *selected = {sample.samples,
                 sample.frameCount,
                 sample.sampleRate,
                 0,
                 0.0F,
                 playbackRate,
                 nextStartOrder_++,
                 static_cast<std::uint8_t>(track),
                 true};
    return true;
}

bool SamplerVoicePool::setTrackPlaybackRate(
    std::size_t track, float playbackRate) noexcept {
    if (track >= TrackCount || !std::isfinite(playbackRate) ||
        playbackRate < MinPlaybackRate || playbackRate > MaxPlaybackRate) {
        return false;
    }
    for (auto& voice : voices_) {
        if (voice.active && voice.track == track) {
            voice.playbackRate = playbackRate;
        }
    }
    return true;
}

bool SamplerVoicePool::renderTrack(std::size_t track,
                                   float* monoOutput,
                                   std::size_t frameCount,
                                   std::uint32_t outputSampleRate) noexcept {
    if (track >= TrackCount || monoOutput == nullptr ||
        frameCount > MaxRenderFrames || outputSampleRate < 8000 ||
        outputSampleRate > 192000) {
        return false;
    }
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        monoOutput[frame] = 0.0F;
    }

    for (auto& voice : voices_) {
        if (!voice.active || voice.track != track) {
            continue;
        }

        const float step =
            static_cast<float>(voice.sourceSampleRate) /
            static_cast<float>(outputSampleRate) * voice.playbackRate;
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            if (voice.positionFrame >= voice.sampleFrameCount) {
                voice.active = false;
                break;
            }

            const auto nextFrame =
                voice.positionFrame + 1 < voice.sampleFrameCount
                    ? voice.positionFrame + 1
                    : voice.positionFrame;
            const float first = voice.samples[voice.positionFrame];
            const float second = voice.samples[nextFrame];
            monoOutput[frame] +=
                first + (second - first) * voice.fractionalPosition;

            voice.fractionalPosition += step;
            const auto wholeFrames =
                static_cast<std::size_t>(voice.fractionalPosition);
            voice.positionFrame += wholeFrames;
            voice.fractionalPosition -= static_cast<float>(wholeFrames);
        }
        if (voice.positionFrame >= voice.sampleFrameCount) {
            voice.active = false;
        }
    }
    return true;
}

void SamplerVoicePool::stopTrack(std::size_t track) noexcept {
    if (track >= TrackCount) {
        return;
    }
    for (auto& voice : voices_) {
        if (voice.active && voice.track == track) {
            voice.active = false;
        }
    }
}

void SamplerVoicePool::stopAll() noexcept {
    for (auto& voice : voices_) {
        voice.active = false;
    }
}

std::size_t SamplerVoicePool::activeVoiceCount() const noexcept {
    std::size_t count = 0;
    for (const auto& voice : voices_) {
        count += voice.active ? 1U : 0U;
    }
    return count;
}

std::size_t SamplerVoicePool::activeVoiceCount(std::size_t track) const noexcept {
    if (track >= TrackCount) {
        return 0;
    }
    std::size_t count = 0;
    for (const auto& voice : voices_) {
        count += voice.active && voice.track == track ? 1U : 0U;
    }
    return count;
}

} // namespace pocket::audio
