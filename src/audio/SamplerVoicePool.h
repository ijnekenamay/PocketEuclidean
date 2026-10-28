#pragma once

#include "sequencer/Sequencer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pocket::audio {

struct MonoSampleView {
    const float* samples = nullptr;
    std::size_t frameCount = 0;
    std::uint32_t sampleRate = 0;
};

class SamplerVoicePool {
public:
    static constexpr std::size_t MaxVoices = 32;
    static constexpr std::uint32_t MaxRenderFrames = 4096;
    static constexpr float MinPlaybackRate = 0.25F;
    static constexpr float MaxPlaybackRate = 4.0F;
    static constexpr std::size_t SampleQueueCapacity = 32;
    static constexpr std::size_t MaxSampleFrames = 16U * 1024U * 1024U;

    SamplerVoicePool() noexcept;

    bool assignSample(std::size_t track,
                      const MonoSampleView& sample) noexcept;

    // UI-thread side: queues a sample swap without touching audio-owned state.
    // The caller keeps the buffer alive until appliedSampleId(track) reaches
    // the returned id (or a newer one).
    bool queueSample(std::size_t track,
                     const MonoSampleView& sample,
                     std::uint32_t id) noexcept;
    // Audio-thread side: stops the track's voices and applies queued swaps.
    void applyPendingSamples() noexcept;
    std::uint32_t appliedSampleId(std::size_t track) const noexcept;
    MonoSampleView sample(std::size_t track) const noexcept;

    bool trigger(std::size_t track,
                 float playbackRate = 1.0F) noexcept;
    // Applies a new playback rate to voices already sounding on the track.
    // Used by the Turing GLIDE smoothing while a note is held.
    bool setTrackPlaybackRate(std::size_t track,
                              float playbackRate) noexcept;
    bool renderTrack(std::size_t track,
                     float* monoOutput,
                     std::size_t frameCount,
                     std::uint32_t outputSampleRate) noexcept;

    void stopTrack(std::size_t track) noexcept;
    void stopAll() noexcept;

    std::size_t activeVoiceCount() const noexcept;
    std::size_t activeVoiceCount(std::size_t track) const noexcept;
    std::uint64_t stolenVoiceCount() const noexcept { return stolenVoiceCount_; }

private:
    struct SampleRequest {
        std::uint8_t track = 0;
        MonoSampleView view{};
        std::uint32_t id = 0;
    };

    struct Voice {
        const float* samples = nullptr;
        std::size_t sampleFrameCount = 0;
        std::uint32_t sourceSampleRate = 0;
        std::size_t positionFrame = 0;
        float fractionalPosition = 0.0F;
        float playbackRate = 1.0F;
        std::uint64_t startOrder = 0;
        std::uint8_t track = 0;
        bool active = false;
    };

    std::array<MonoSampleView, TrackCount> samples_{};
    std::array<Voice, MaxVoices> voices_{};
    std::uint64_t nextStartOrder_ = 0;
    std::uint64_t stolenVoiceCount_ = 0;

    std::array<SampleRequest, SampleQueueCapacity> sampleRequests_{};
    alignas(64) std::atomic<std::uint32_t> sampleWriteIndex_{0};
    alignas(64) std::atomic<std::uint32_t> sampleReadIndex_{0};
    std::array<std::atomic<std::uint32_t>, TrackCount> appliedSampleIds_{};
};

} // namespace pocket::audio
