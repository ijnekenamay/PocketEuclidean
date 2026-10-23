#include "ui/StepLightBank.h"

#include <cmath>
#include <limits>

namespace pocket::ui {

bool StepLightBank::consume(StepTriggerEventQueue& queue,
                            std::uint64_t currentAudioFrame,
                            std::uint32_t sampleRate) noexcept {
    if (sampleRate == 0) {
        return false;
    }

    while (true) {
        if (!hasPendingEvent_) {
            if (!queue.pop(pendingEvent_)) {
                return true;
            }
            hasPendingEvent_ = true;
        }

        if (pendingEvent_.audioFrame > currentAudioFrame) {
            return true;
        }

        const auto event = pendingEvent_;
        hasPendingEvent_ = false;

        const double ageSeconds =
            static_cast<double>(currentAudioFrame - event.audioFrame) /
            static_cast<double>(sampleRate);
        if (!isValid(event) || ageSeconds > EnvelopeDurationSeconds) {
            ++staleEventCount_;
            continue;
        }

        const double timestampSeconds =
            static_cast<double>(event.audioFrame) /
            static_cast<double>(sampleRate);
        for (std::size_t track = 0; track < TrackCount; ++track) {
            if ((event.triggeredMask & (1U << track)) == 0) {
                continue;
            }

            auto& envelope = envelopes_[track][event.stepPositions[track]];
            if (!envelope.trigger(timestampSeconds)) {
                ++staleEventCount_;
            }
        }
    }
}

bool StepLightBank::valueAt(std::size_t track,
                            std::size_t step,
                            std::uint64_t audioFrame,
                            std::uint32_t sampleRate,
                            float& luminance) const noexcept {
    if (track >= TrackCount || step >= EuclideanGenerator::MaxLength ||
        sampleRate == 0) {
        return false;
    }

    const double timestampSeconds =
        static_cast<double>(audioFrame) / static_cast<double>(sampleRate);
    return envelopes_[track][step].valueAt(timestampSeconds, luminance);
}

void StepLightBank::reset() noexcept {
    hasPendingEvent_ = false;
    pendingEvent_ = {};
    envelopes_ = {};
    staleEventCount_ = 0;
}

bool StepLightBank::isValid(const StepTriggerEvent& event) const noexcept {
    if (event.triggeredMask == 0) {
        return false;
    }

    for (std::size_t track = 0; track < TrackCount; ++track) {
        if ((event.triggeredMask & (1U << track)) != 0 &&
            event.stepPositions[track] >= EuclideanGenerator::MaxLength) {
            return false;
        }
    }
    return true;
}

} // namespace pocket::ui
