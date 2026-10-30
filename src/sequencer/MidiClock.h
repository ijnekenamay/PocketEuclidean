#pragma once

#include "platform/BackendInterfaces.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket {

// Follows an external MIDI clock (24 pulses per quarter note) and converts it
// into transport requests plus a smoothed tempo. This type owns no OS or MIDI
// library state, performs no allocation, and is safe to drive from the UI
// thread after the backend has stamped each event.
class MidiClockFollower {
public:
    static constexpr double MinTempoBpm = 40.0;
    static constexpr double MaxTempoBpm = 240.0;

    void reset() noexcept;

    // Consumes one received event. Transport requests are latched until taken.
    void process(const platform::MidiEvent& event) noexcept;

    bool takeStart() noexcept;
    bool takeStop() noexcept;
    bool takeResume() noexcept;
    bool takeTempoChanged() noexcept;

    bool running() const noexcept { return running_; }
    bool clockSeen() const noexcept { return clockCount_ != 0; }
    std::uint64_t clockCount() const noexcept { return clockCount_; }
    double tempoBpm() const noexcept { return tempoBpm_; }
    std::size_t tempoIntervalCount() const noexcept { return intervalCount_; }

private:
    static constexpr std::size_t TempoWindow = 24;
    // 2 ms..250 ms per clock spans roughly 10..1250 BPM. Tempo is clamped to
    // the sequencer range; out-of-range intervals reset the estimator instead
    // of dragging it toward an impossible value.
    static constexpr std::uint64_t MinIntervalMicros = 2000;
    static constexpr std::uint64_t MaxIntervalMicros = 250000;

    void onClock(std::uint64_t timestampMicros) noexcept;
    void clearIntervals() noexcept;
    void updateTempo() noexcept;

    std::array<std::uint64_t, TempoWindow> intervals_{};
    std::size_t intervalWrite_ = 0;
    std::size_t intervalCount_ = 0;
    std::uint64_t intervalSum_ = 0;
    std::uint64_t lastClockMicros_ = 0;
    std::uint64_t clockCount_ = 0;
    double tempoBpm_ = 120.0;
    // Last tempo handed to the caller. The default 120 is only a placeholder;
    // the first real estimate must be published even when it equals 120 so the
    // sequencer does not keep an unrelated stored tempo.
    double reportedTempoBpm_ = 0.0;
    bool haveLastClock_ = false;
    bool running_ = false;
    bool startPending_ = false;
    bool stopPending_ = false;
    bool resumePending_ = false;
    bool tempoChangedPending_ = false;
};

// Generates 24 PPQN clock pulses from the master tempo. It runs independently
// of transport state, so F8 keeps flowing while the sequencer is stopped. The
// callback is invoked from the audio thread; sampleOffset is relative to the
// current block.
class MidiClockGenerator {
public:
    using PulseCallback = void (*)(void* userData,
                                   std::uint32_t sampleOffset) noexcept;

    void reset() noexcept;

    void process(std::uint32_t frameCount,
                 std::uint32_t sampleRate,
                 double bpm,
                 PulseCallback callback,
                 void* userData) noexcept;

    std::uint64_t pulseCount() const noexcept { return pulseCount_; }

private:
    double framesUntilNextPulse_ = 0.0;
    std::uint32_t sampleRate_ = 0;
    std::uint64_t pulseCount_ = 0;
};

} // namespace pocket
