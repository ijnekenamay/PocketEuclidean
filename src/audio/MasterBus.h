#pragma once

#include <cstddef>
#include <cstdint>

namespace pocket::audio {

// Fixed master-bus treatment applied after the delay line, so every source
// (sampler, synth, reverb, delay) shares the same tone and width. It is a
// deliberately small, non-user-editable stage: a low shelf for body, a high
// shelf for presence, a gentle mid/side width, and a final safety limiter.
class MasterBus {
public:
    static constexpr float LowShelfHz = 160.0F;
    static constexpr float LowShelfGainDb = 2.5F;
    static constexpr float HighShelfHz = 4000.0F;
    static constexpr float HighShelfGainDb = 1.5F;
    static constexpr float StereoWidth = 1.2F;
    // Below the threshold the signal is untouched; above it the curve bends
    // smoothly towards 1.0 so the EQ and effect sum cannot hard-clip.
    static constexpr float LimitThreshold = 0.95F;
    // Flush the IIR tail and denormals once the bus is inaudible. Without this
    // a silent block would keep an exponentially decaying residue forever.
    static constexpr float SilenceFloor = 1.0e-7F;

    void reset() noexcept;
    bool prepare(std::uint32_t sampleRate) noexcept;
    void process(float* interleavedStereo, std::size_t frameCount) noexcept;
    bool isPrepared() const noexcept { return prepared_; }

private:
    struct Biquad {
        float b0 = 1.0F;
        float b1 = 0.0F;
        float b2 = 0.0F;
        float a1 = 0.0F;
        float a2 = 0.0F;
    };

    struct BiquadState {
        float z1 = 0.0F;
        float z2 = 0.0F;
    };

    static Biquad makeShelf(bool high, float hz, float gainDb,
                            std::uint32_t sampleRate) noexcept;
    static float run(const Biquad& filter, BiquadState& state,
                     float sample) noexcept;
    static float limit(float sample) noexcept;

    Biquad lowShelf_{};
    Biquad highShelf_{};
    BiquadState lowStateLeft_{};
    BiquadState lowStateRight_{};
    BiquadState highStateLeft_{};
    BiquadState highStateRight_{};
    std::uint32_t sampleRate_ = 0;
    bool prepared_ = false;
};

} // namespace pocket::audio
