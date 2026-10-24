#pragma once

#include "project/ProjectData.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pocket::audio {

class DelayEffect {
public:
    static constexpr std::size_t MaxDelayFrames = 96000; // 2000 ms @ 48 kHz
    static constexpr std::uint16_t MinDelayMs = 10;
    static constexpr std::uint16_t MaxDelayMs = 2000;
    static constexpr float MaxFeedback = 0.90F;

    DelayEffect()
        : bufferL_(MaxDelayFrames, 0.0F),
          bufferR_(MaxDelayFrames, 0.0F) {}

    void setParameters(const project::ProjectEffectState& state) noexcept {
        delayMs_ = std::clamp(state.delayMilliseconds, MinDelayMs, MaxDelayMs);
        feedback_ = std::clamp(state.delayFeedback, 0.0F, MaxFeedback);
        mix_ = std::clamp(state.delayMix, 0.0F, 1.0F);
    }

    void reset() noexcept {
        std::fill(bufferL_.begin(), bufferL_.end(), 0.0F);
        std::fill(bufferR_.begin(), bufferR_.end(), 0.0F);
        writeIndex_ = 0;
    }

    // Processes interleaved stereo in-place without allocation or blocking.
    void processStereo(float* interleavedStereo,
                       std::size_t frameCount,
                       std::uint32_t sampleRate) noexcept {
        if (interleavedStereo == nullptr || frameCount == 0 ||
            sampleRate < 8000 || sampleRate > 192000 || mix_ <= 0.0F) {
            return;
        }

        const auto delayFrames = std::clamp(
            static_cast<std::size_t>(
                (static_cast<std::uint64_t>(delayMs_) * sampleRate) / 1000U),
            std::size_t{1}, MaxDelayFrames - 1);

        const float dryGain = 1.0F - mix_;
        const float wetGain = mix_;
        const float fb = feedback_;

        for (std::size_t i = 0; i < frameCount; ++i) {
            const std::size_t readIndex =
                (writeIndex_ + MaxDelayFrames - delayFrames) % MaxDelayFrames;

            const float inL = interleavedStereo[i * 2];
            const float inR = interleavedStereo[i * 2 + 1];

            const float delayedL = bufferL_[readIndex];
            const float delayedR = bufferR_[readIndex];

            // Soft clamp in feedback loop to protect against runaway
            bufferL_[writeIndex_] = std::clamp(inL + delayedL * fb, -2.0F, 2.0F);
            bufferR_[writeIndex_] = std::clamp(inR + delayedR * fb, -2.0F, 2.0F);

            interleavedStereo[i * 2] = inL * dryGain + delayedL * wetGain;
            interleavedStereo[i * 2 + 1] = inR * dryGain + delayedR * wetGain;

            writeIndex_ = (writeIndex_ + 1) % MaxDelayFrames;
        }
    }

    bool isRealtimeSafe() const noexcept { return true; }

    std::uint16_t delayMilliseconds() const noexcept { return delayMs_; }
    float feedback() const noexcept { return feedback_; }
    float mix() const noexcept { return mix_; }

private:
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    std::size_t writeIndex_ = 0;
    std::uint16_t delayMs_ = 250;
    float feedback_ = 0.25F;
    float mix_ = 0.2F;
};

} // namespace pocket::audio
