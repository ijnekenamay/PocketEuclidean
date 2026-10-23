#pragma once

#include "sequencer/EuclideanGenerator.h"
#include "ui/StepLightEnvelope.h"
#include "ui/StepTriggerEventQueue.h"

#include <array>
#include <cstdint>

namespace pocket::ui {

class StepLightBank {
public:
    static constexpr double EnvelopeDurationSeconds =
        StepLightEnvelope::AttackSeconds +
        StepLightEnvelope::PeakHoldSeconds +
        StepLightEnvelope::DecaySeconds;

    bool consume(StepTriggerEventQueue& queue,
                 std::uint64_t currentAudioFrame,
                 std::uint32_t sampleRate) noexcept;

    bool valueAt(std::size_t track,
                 std::size_t step,
                 std::uint64_t audioFrame,
                 std::uint32_t sampleRate,
                 float& luminance) const noexcept;

    void reset() noexcept;
    std::uint64_t staleEventCount() const noexcept {
        return staleEventCount_;
    }

private:
    bool isValid(const StepTriggerEvent& event) const noexcept;
    bool hasPendingEvent_ = false;
    StepTriggerEvent pendingEvent_{};
    std::array<std::array<StepLightEnvelope, EuclideanGenerator::MaxLength>,
               TrackCount>
        envelopes_{};
    std::uint64_t staleEventCount_ = 0;
};

} // namespace pocket::ui
