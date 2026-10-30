#include "sequencer/MidiClock.h"

#include <algorithm>
#include <cmath>

namespace pocket {

void MidiClockFollower::reset() noexcept {
    intervals_.fill(0);
    intervalWrite_ = 0;
    intervalCount_ = 0;
    intervalSum_ = 0;
    lastClockMicros_ = 0;
    clockCount_ = 0;
    tempoBpm_ = 120.0;
    reportedTempoBpm_ = 0.0;
    haveLastClock_ = false;
    running_ = false;
    startPending_ = false;
    stopPending_ = false;
    resumePending_ = false;
    tempoChangedPending_ = false;
}

void MidiClockFollower::process(const platform::MidiEvent& event) noexcept {
    using Type = platform::MidiEvent::Type;
    switch (event.type) {
    case Type::Start:
        startPending_ = true;
        stopPending_ = false;
        resumePending_ = false;
        running_ = true;
        haveLastClock_ = false;
        clearIntervals();
        break;
    case Type::Stop:
        stopPending_ = true;
        startPending_ = false;
        resumePending_ = false;
        running_ = false;
        haveLastClock_ = false;
        clearIntervals();
        break;
    case Type::Continue:
        resumePending_ = true;
        startPending_ = false;
        stopPending_ = false;
        running_ = true;
        haveLastClock_ = false;
        clearIntervals();
        break;
    case Type::Clock:
        onClock(event.timestampMicros);
        break;
    default:
        break;
    }
}

void MidiClockFollower::onClock(std::uint64_t timestampMicros) noexcept {
    ++clockCount_;
    if (timestampMicros == 0) {
        return;
    }
    if (!haveLastClock_ || timestampMicros <= lastClockMicros_) {
        lastClockMicros_ = timestampMicros;
        haveLastClock_ = true;
        return;
    }

    const auto interval = timestampMicros - lastClockMicros_;
    lastClockMicros_ = timestampMicros;
    if (interval < MinIntervalMicros || interval > MaxIntervalMicros) {
        clearIntervals();
        return;
    }

    if (intervalCount_ >= 4) {
        const auto average =
            static_cast<double>(intervalSum_) /
            static_cast<double>(intervalCount_);
        const auto value = static_cast<double>(interval);
        if (value > average * 3.0 || value * 3.0 < average) {
            // A transport jump or a long scheduling gap must not bend the
            // tempo estimate; rebuild the window from the next intervals.
            clearIntervals();
            return;
        }
    }

    if (intervalCount_ == TempoWindow) {
        intervalSum_ -= intervals_[intervalWrite_];
    } else {
        ++intervalCount_;
    }
    intervals_[intervalWrite_] = interval;
    intervalSum_ += interval;
    intervalWrite_ = (intervalWrite_ + 1) % TempoWindow;
    updateTempo();
}

void MidiClockFollower::clearIntervals() noexcept {
    intervals_.fill(0);
    intervalWrite_ = 0;
    intervalCount_ = 0;
    intervalSum_ = 0;
}

void MidiClockFollower::updateTempo() noexcept {
    if (intervalCount_ == 0) {
        return;
    }
    const auto averageMicros =
        static_cast<double>(intervalSum_) / static_cast<double>(intervalCount_);
    if (averageMicros <= 0.0) {
        return;
    }
    const double bpm = 60000000.0 / (averageMicros * 24.0);
    const double clamped = std::clamp(bpm, MinTempoBpm, MaxTempoBpm);
    if (std::abs(clamped - tempoBpm_) >= 0.001) {
        tempoBpm_ = clamped;
    }
    if (reportedTempoBpm_ == 0.0 ||
        std::abs(clamped - reportedTempoBpm_) >= 0.05) {
        tempoChangedPending_ = true;
    }
}

bool MidiClockFollower::takeStart() noexcept {
    const bool value = startPending_;
    startPending_ = false;
    return value;
}

bool MidiClockFollower::takeStop() noexcept {
    const bool value = stopPending_;
    stopPending_ = false;
    return value;
}

bool MidiClockFollower::takeResume() noexcept {
    const bool value = resumePending_;
    resumePending_ = false;
    return value;
}

bool MidiClockFollower::takeTempoChanged() noexcept {
    const bool value = tempoChangedPending_;
    if (value) {
        reportedTempoBpm_ = tempoBpm_;
    }
    tempoChangedPending_ = false;
    return value;
}

void MidiClockGenerator::reset() noexcept {
    framesUntilNextPulse_ = 0.0;
    sampleRate_ = 0;
    pulseCount_ = 0;
}

void MidiClockGenerator::process(std::uint32_t frameCount,
                                 std::uint32_t sampleRate,
                                 double bpm,
                                 PulseCallback callback,
                                 void* userData) noexcept {
    if (frameCount == 0 || sampleRate == 0) {
        return;
    }
    if (sampleRate != sampleRate_) {
        sampleRate_ = sampleRate;
        framesUntilNextPulse_ = 0.0;
    }
    const double safeBpm = std::clamp(bpm, 20.0, 400.0);
    const double framesPerPulse =
        static_cast<double>(sampleRate) * 60.0 / (safeBpm * 24.0);
    if (!(framesPerPulse > 0.0)) {
        return;
    }
    while (framesUntilNextPulse_ < static_cast<double>(frameCount)) {
        const auto offset =
            static_cast<std::uint32_t>(std::ceil(framesUntilNextPulse_));
        if (callback != nullptr) {
            callback(userData, offset);
        }
        ++pulseCount_;
        framesUntilNextPulse_ += framesPerPulse;
    }
    framesUntilNextPulse_ -= static_cast<double>(frameCount);
}

} // namespace pocket
