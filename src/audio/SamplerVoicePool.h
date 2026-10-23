#pragma once

#include "sequencer/Sequencer.h"

#include <array>
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

    bool assignSample(std::size_t track,
                      const MonoSampleView& sample) noexcept;
    bool trigger(std::size_t track,
                 float playbackRate = 1.0F) noexcept;
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
};

} // namespace pocket::audio
