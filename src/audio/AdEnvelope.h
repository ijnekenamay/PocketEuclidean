#pragma once

#include "sequencer/Sequencer.h"

#include <cstdint>

namespace pocket::audio {

class AdEnvelope {
public:
    bool trigger(const AdEnvelopeSettings& settings,
                 std::uint32_t sampleRate) noexcept;
    bool updateSettings(const AdEnvelopeSettings& settings) noexcept;
    float process(float input) noexcept;
    void reset() noexcept;
    bool isActive() const noexcept;

private:
    enum class Stage : std::uint8_t {
        Idle,
        Attack,
        Decay
    };

    float level_ = 0.0F;
    float attackStep_ = 0.0F;
    float decayStep_ = 0.0F;
    AdEnvelopeSettings settings_{};
    std::uint32_t sampleRate_ = 0;
    Stage stage_ = Stage::Idle;
};

} // namespace pocket::audio
