#pragma once

#include <cstddef>

namespace pocket::audio {

// Streams a fixed-rate DSP core (48 kHz for Plaits and Rings) out at the
// device rate with linear interpolation. Source::next() must return the next
// sample at the source rate. No allocation; safe on the audio thread.
class LinearStreamResampler {
public:
    void reset() noexcept {
        position_ = 1.0;
        previous_ = 0.0F;
        current_ = 0.0F;
    }

    template <typename Source>
    void render(float* output,
                std::size_t frames,
                double sourceRate,
                double outputRate,
                Source& source) noexcept {
        const double step = sourceRate / outputRate;
        for (std::size_t i = 0; i < frames; ++i) {
            while (position_ >= 1.0) {
                previous_ = current_;
                current_ = source.next();
                position_ -= 1.0;
            }
            output[i] = previous_ +
                        (current_ - previous_) * static_cast<float>(position_);
            position_ += step;
        }
    }

private:
    double position_ = 1.0;
    float previous_ = 0.0F;
    float current_ = 0.0F;
};

} // namespace pocket::audio
