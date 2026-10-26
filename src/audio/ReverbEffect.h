#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pocket::audio {

// Small, allocation-free Schroeder reverb used as a global effect send bus.
// The send buffers are already at unity-independent level, so the effect is
// 100% wet: processSendStereo replaces its input with the reverberated signal.
class ReverbEffect {
public:
    static constexpr std::size_t MaxCombFrames = 8192;
    static constexpr std::size_t MaxAllpassFrames = 4096;
    static constexpr std::size_t CombCount = 4;
    static constexpr std::size_t AllpassCount = 2;

    ReverbEffect() noexcept = default;

    void reset() noexcept;
    void processSendStereo(float* interleavedStereo,
                           std::size_t frameCount,
                           std::uint32_t sampleRate) noexcept;
    bool isRealtimeSafe() const noexcept { return true; }

private:
    struct Comb {
        // Heap storage keeps large effect state off the audio thread stack;
        // allocation happens once at construction, never in process().
        std::vector<float> buffer = std::vector<float>(MaxCombFrames, 0.0F);
        std::size_t size = 0;
        std::size_t index = 0;
        float dampingStore = 0.0F;
    };

    struct Allpass {
        std::vector<float> buffer =
            std::vector<float>(MaxAllpassFrames, 0.0F);
        std::size_t size = 0;
        std::size_t index = 0;
    };

    void configure(std::uint32_t sampleRate) noexcept;
    float processComb(Comb& comb, float input, float feedback,
                      float damping) noexcept;
    float processAllpass(Allpass& allpass, float input) noexcept;

    std::array<Comb, CombCount> combLeft_{};
    std::array<Comb, CombCount> combRight_{};
    std::array<Allpass, AllpassCount> allpassLeft_{};
    std::array<Allpass, AllpassCount> allpassRight_{};
    std::uint32_t configuredRate_ = 0;
};

} // namespace pocket::audio
