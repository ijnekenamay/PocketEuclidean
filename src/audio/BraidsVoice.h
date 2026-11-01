#pragma once

#include "audio/PlaitsVoice.h"
#include "audio/SynthVoice.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace pocket::audio {

// Per-track voice running the real Braids macro oscillator
// (third_party/mutable). Braids renders at 96 kHz; the output is low-passed,
// decimated to 48 kHz and resampled to the device rate.
//
// Braids has two parameters: timbre and color. morph is unused. model selects
// the macro-oscillator shape (0..46). The oscillator runs continuously after
// a note is triggered; shaping is left to the track envelope.
class BraidsVoice final : public ISynthVoice {
public:
    BraidsVoice();
    ~BraidsVoice() override;
    BraidsVoice(const BraidsVoice&) = delete;
    BraidsVoice& operator=(const BraidsVoice&) = delete;

    bool setParameters(const SynthParameters& parameters) noexcept override;
    bool noteOn(float velocity) noexcept override;
    void noteOff() noexcept override;
    bool render(float* monoOutput,
                std::size_t frameCount,
                std::uint32_t sampleRate) noexcept override;
    bool isRealtimeSafe() const noexcept override { return true; }

    bool isActive() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pocket::audio
