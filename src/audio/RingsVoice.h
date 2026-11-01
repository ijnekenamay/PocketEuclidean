#pragma once

#include "audio/PlaitsVoice.h"
#include "audio/SynthVoice.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace pocket::audio {

// Per-track voice running the real Rings resonator (third_party/mutable) with
// its internal exciter. Each note is a strum. Rings is a 48 kHz design, so
// its output is resampled to the device rate.
//
// Parameter roles follow the hardware: color is STRUCTURE, timbre is
// BRIGHTNESS, morph is DAMPING and POSITION is fixed. model selects the
// resonator (0..5): modal, sympathetic strings, inharmonic string, FM voice,
// quantized sympathetic strings, and string with reverb.
class RingsVoice final : public ISynthVoice {
public:
    RingsVoice();
    ~RingsVoice() override;
    RingsVoice(const RingsVoice&) = delete;
    RingsVoice& operator=(const RingsVoice&) = delete;

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
