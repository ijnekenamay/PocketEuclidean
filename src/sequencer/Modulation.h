#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace pocket {

enum class LfoWaveform : std::uint8_t {
    Sine,
    Triangle,
    Square,
    SawUp,
    SawDown,
    SampleAndHold
};

enum class LfoRateMode : std::uint8_t {
    Free,
    Synchronized
};

enum class LfoDivision : std::uint8_t {
    Whole,
    DottedWhole,
    WholeTriplet,
    Half,
    DottedHalf,
    HalfTriplet,
    Quarter,
    DottedQuarter,
    QuarterTriplet,
    Eighth,
    DottedEighth,
    EighthTriplet,
    Sixteenth,
    DottedSixteenth,
    SixteenthTriplet,
    Count
};

enum class ModulationDestination : std::uint8_t {
    Length,
    Fill,
    Offset
};

struct LfoConfig {
    bool enabled = false;
    LfoWaveform waveform = LfoWaveform::Sine;
    LfoRateMode rateMode = LfoRateMode::Free;
    double rateHz = 0.25;
    LfoDivision division = LfoDivision::Quarter;
    ModulationDestination destination = ModulationDestination::Length;
    std::int8_t amount = 0;
};

constexpr std::size_t ModulationTrackCount = 8;
constexpr std::size_t MaxLfosPerTrack = 4;
constexpr std::size_t InitialLfosPerTrack = 1;

struct EffectiveRhythm {
    std::uint8_t length = 16;
    std::uint8_t fill = 4;
    std::uint8_t offset = 0;
};

struct LfoRuntime {
    double phase = 0.0;
    std::uint32_t randomState = 0x9e3779b9U;
    double heldValue = 0.0;
};

class LfoRateCalculator {
public:
    static double cycleQuarterNotes(LfoDivision division) noexcept;
    static std::optional<double> phaseIncrement(const LfoConfig& config,
                                                double elapsedSeconds,
                                                double tempoBpm) noexcept;
};

class LfoWaveformGenerator {
public:
    static double value(LfoWaveform waveform,
                        double phase,
                        double heldValue) noexcept;
};

class ModulationRouter {
public:
    static bool isValid(const LfoConfig& config) noexcept;
    static EffectiveRhythm apply(const EffectiveRhythm& base,
                                 double lengthModulation,
                                 double fillModulation,
                                 double offsetModulation) noexcept;
};

class ModulationEngine {
public:
    static constexpr double MinFreeRateHz = 0.01;
    static constexpr double MaxFreeRateHz = 1.0;

    ModulationEngine() noexcept;

    static bool isValid(const LfoConfig& config) noexcept;

    void reset() noexcept;
    std::optional<EffectiveRhythm> advance(
        std::size_t track,
        const std::array<LfoConfig, MaxLfosPerTrack>& configs,
        const EffectiveRhythm& base,
        double elapsedSeconds,
        double tempoBpm) noexcept;
    std::optional<EffectiveRhythm> advance(std::size_t track,
                                           const LfoConfig& config,
                                           const EffectiveRhythm& base,
                                           double elapsedSeconds,
                                           double tempoBpm) noexcept;

private:
    std::array<std::array<LfoRuntime, MaxLfosPerTrack>, ModulationTrackCount>
        runtimes_{};
};

} // namespace pocket
