#include "audio/MasterBus.h"

#include <cmath>

namespace pocket::audio {
namespace {

constexpr float Pi = 3.14159265358979323846F;

} // namespace

// RBJ audio-EQ-cookbook shelf pair with S = 1. The gain is stored in dB and
// converted to the linear A term once, in prepare().
MasterBus::Biquad MasterBus::makeShelf(bool high, float hz, float gainDb,
                                       std::uint32_t sampleRate) noexcept {
    const float a = std::pow(10.0F, gainDb / 40.0F);
    const float w0 = 2.0F * Pi * hz / static_cast<float>(sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW * 0.5F * std::sqrt(2.0F);
    const float twoSqrtAAlpha = 2.0F * std::sqrt(a) * alpha;

    float b0 = 0.0F;
    float b1 = 0.0F;
    float b2 = 0.0F;
    float a0 = 0.0F;
    float a1 = 0.0F;
    float a2 = 0.0F;
    if (high) {
        b0 = a * ((a + 1.0F) + (a - 1.0F) * cosW + twoSqrtAAlpha);
        b1 = -2.0F * a * ((a - 1.0F) + (a + 1.0F) * cosW);
        b2 = a * ((a + 1.0F) + (a - 1.0F) * cosW - twoSqrtAAlpha);
        a0 = (a + 1.0F) - (a - 1.0F) * cosW + twoSqrtAAlpha;
        a1 = 2.0F * ((a - 1.0F) - (a + 1.0F) * cosW);
        a2 = (a + 1.0F) - (a - 1.0F) * cosW - twoSqrtAAlpha;
    } else {
        b0 = a * ((a + 1.0F) - (a - 1.0F) * cosW + twoSqrtAAlpha);
        b1 = 2.0F * a * ((a - 1.0F) - (a + 1.0F) * cosW);
        b2 = a * ((a + 1.0F) - (a - 1.0F) * cosW - twoSqrtAAlpha);
        a0 = (a + 1.0F) + (a - 1.0F) * cosW + twoSqrtAAlpha;
        a1 = -2.0F * ((a - 1.0F) + (a + 1.0F) * cosW);
        a2 = (a + 1.0F) + (a - 1.0F) * cosW - twoSqrtAAlpha;
    }

    Biquad filter;
    const float inverseA0 = 1.0F / a0;
    filter.b0 = b0 * inverseA0;
    filter.b1 = b1 * inverseA0;
    filter.b2 = b2 * inverseA0;
    filter.a1 = a1 * inverseA0;
    filter.a2 = a2 * inverseA0;
    return filter;
}

void MasterBus::reset() noexcept {
    lowStateLeft_ = {};
    lowStateRight_ = {};
    highStateLeft_ = {};
    highStateRight_ = {};
}

bool MasterBus::prepare(std::uint32_t sampleRate) noexcept {
    if (sampleRate < 8000 || sampleRate > 192000) {
        prepared_ = false;
        return false;
    }
    if (prepared_ && sampleRate_ == sampleRate) {
        return true;
    }
    sampleRate_ = sampleRate;
    lowShelf_ = makeShelf(false, LowShelfHz, LowShelfGainDb, sampleRate);
    highShelf_ = makeShelf(true, HighShelfHz, HighShelfGainDb, sampleRate);
    reset();
    prepared_ = true;
    return true;
}

float MasterBus::run(const Biquad& filter, BiquadState& state,
                     float sample) noexcept {
    // Transposed direct form II keeps two state words and stays stable for
    // the fixed shelf coefficients computed in prepare().
    const float output = filter.b0 * sample + state.z1;
    state.z1 = filter.b1 * sample - filter.a1 * output + state.z2;
    state.z2 = filter.b2 * sample - filter.a2 * output;
    return output;
}

float MasterBus::limit(float sample) noexcept {
    const float magnitude = std::abs(sample);
    if (magnitude <= LimitThreshold) {
        return sample;
    }
    const float over = (magnitude - LimitThreshold) /
                       (1.0F - LimitThreshold);
    // Rational saturator: cheap, monotonic, and asymptotically bounded by 1.
    const float shaped = LimitThreshold +
                         (1.0F - LimitThreshold) * (over / (1.0F + over));
    return std::copysign(shaped, sample);
}

void MasterBus::process(float* interleavedStereo,
                        std::size_t frameCount) noexcept {
    if (!prepared_ || interleavedStereo == nullptr) {
        return;
    }
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        float left = interleavedStereo[frame * 2];
        float right = interleavedStereo[frame * 2 + 1];

        left = run(lowShelf_, lowStateLeft_, left);
        right = run(lowShelf_, lowStateRight_, right);
        left = run(highShelf_, highStateLeft_, left);
        right = run(highShelf_, highStateRight_, right);

        // Mid/side width. The mid channel is untouched, so summing the pair
        // back to mono returns the pre-widened signal.
        const float mid = (left + right) * 0.5F;
        const float side = (left - right) * 0.5F * StereoWidth;
        left = mid + side;
        right = mid - side;

        left = limit(left);
        right = limit(right);
        interleavedStereo[frame * 2] =
            std::abs(left) < SilenceFloor ? 0.0F : left;
        interleavedStereo[frame * 2 + 1] =
            std::abs(right) < SilenceFloor ? 0.0F : right;
    }
}

} // namespace pocket::audio
