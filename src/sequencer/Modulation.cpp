#include "sequencer/Modulation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pocket {
namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr std::uint64_t MaxSampleHoldCyclesPerUpdate = 64;
constexpr std::array<double, static_cast<std::size_t>(LfoDivision::Count)>
    CycleQuarterNotes{{
        4.0, 6.0, 8.0 / 3.0,
        2.0, 3.0, 4.0 / 3.0,
        1.0, 1.5, 2.0 / 3.0,
        0.5, 0.75, 1.0 / 3.0,
        0.25, 0.375, 1.0 / 6.0,
    }};

} // namespace

double LfoRateCalculator::cycleQuarterNotes(LfoDivision division) noexcept {
    const auto index = static_cast<std::size_t>(division);
    return index < CycleQuarterNotes.size() ? CycleQuarterNotes[index] : 0.0;
}

std::optional<double> LfoRateCalculator::phaseIncrement(
    const LfoConfig& config,
    double elapsedSeconds,
    double tempoBpm) noexcept {
    if (!ModulationRouter::isValid(config) ||
        !std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 ||
        !std::isfinite(tempoBpm) || tempoBpm <= 0.0) {
        return std::nullopt;
    }
    if (config.rateMode == LfoRateMode::Free) {
        return elapsedSeconds * config.rateHz;
    }
    const double cycleSeconds =
        cycleQuarterNotes(config.division) * 60.0 / tempoBpm;
    return elapsedSeconds / cycleSeconds;
}

double LfoWaveformGenerator::value(LfoWaveform waveform,
                                   double phase,
                                   double heldValue) noexcept {
    switch (waveform) {
    case LfoWaveform::Sine:
        return std::sin(2.0 * Pi * phase);
    case LfoWaveform::Triangle:
        return 1.0 - 4.0 * std::abs(phase - 0.5);
    case LfoWaveform::Square:
        return phase < 0.5 ? 1.0 : -1.0;
    case LfoWaveform::SawUp:
        return 2.0 * phase - 1.0;
    case LfoWaveform::SawDown:
        return 1.0 - 2.0 * phase;
    case LfoWaveform::SampleAndHold:
        return heldValue;
    }
    return 0.0;
}

bool ModulationRouter::isValid(const LfoConfig& config) noexcept {
    return std::isfinite(config.rateHz) &&
           config.rateHz >= ModulationEngine::MinFreeRateHz &&
           config.rateHz <= ModulationEngine::MaxFreeRateHz &&
           config.amount >= -31 && config.amount <= 31 &&
           static_cast<std::uint8_t>(config.waveform) <=
               static_cast<std::uint8_t>(LfoWaveform::SampleAndHold) &&
           static_cast<std::uint8_t>(config.rateMode) <=
               static_cast<std::uint8_t>(LfoRateMode::Synchronized) &&
           static_cast<std::uint8_t>(config.division) <
               static_cast<std::uint8_t>(LfoDivision::Count) &&
           static_cast<std::uint8_t>(config.destination) <=
               static_cast<std::uint8_t>(ModulationDestination::Offset);
}

EffectiveRhythm ModulationRouter::apply(
    const EffectiveRhythm& base,
    double lengthModulation,
    double fillModulation,
    double offsetModulation) noexcept {
    const int length = std::clamp(
        static_cast<int>(std::lround(base.length + lengthModulation)), 2, 32);
    const int fill = std::clamp(
        static_cast<int>(std::lround(base.fill + fillModulation)), 1, length);
    const int offset = std::clamp(
        static_cast<int>(std::lround(base.offset + offsetModulation)), 0, 31);
    return {static_cast<std::uint8_t>(length),
            static_cast<std::uint8_t>(fill),
            static_cast<std::uint8_t>(offset)};
}

ModulationEngine::ModulationEngine() noexcept {
    reset();
}

bool ModulationEngine::isValid(const LfoConfig& config) noexcept {
    return ModulationRouter::isValid(config);
}

void ModulationEngine::reset() noexcept {
    for (std::size_t track = 0; track < runtimes_.size(); ++track) {
        for (std::size_t lfo = 0; lfo < runtimes_[track].size(); ++lfo) {
            auto& runtime = runtimes_[track][lfo];
            runtime = {};
            runtime.randomState =
                0x9e3779b9U ^
                static_cast<std::uint32_t>(
                    track * 0x85ebca6bU + lfo * 0xc2b2ae35U);
            if (runtime.randomState == 0) {
                runtime.randomState = 1;
            }
        }
    }
}

std::optional<EffectiveRhythm> ModulationEngine::advance(
    std::size_t track,
    const std::array<LfoConfig, MaxLfosPerTrack>& configs,
    const EffectiveRhythm& base,
    double elapsedSeconds,
    double tempoBpm) noexcept {
    if (track >= runtimes_.size() || !std::isfinite(elapsedSeconds) ||
        elapsedSeconds < 0.0 || !std::isfinite(tempoBpm) || tempoBpm <= 0.0) {
        return std::nullopt;
    }

    std::array<double, 3> contributions{};
    std::array<double, MaxLfosPerTrack> advancedPhases{};
    std::array<std::uint64_t, MaxLfosPerTrack> completedCycleCounts{};
    for (std::size_t lfo = 0; lfo < configs.size(); ++lfo) {
        const auto& config = configs[lfo];
        if (!isValid(config)) {
            return std::nullopt;
        }
        if (!config.enabled || elapsedSeconds == 0.0) {
            continue;
        }

        auto& runtime = runtimes_[track][lfo];
        const auto increment =
            LfoRateCalculator::phaseIncrement(config, elapsedSeconds, tempoBpm);
        if (!increment || !std::isfinite(*increment)) {
            return std::nullopt;
        }

        const double advancedPhase = runtime.phase + *increment;
        if (!std::isfinite(advancedPhase)) {
            return std::nullopt;
        }
        const double completedCycles = std::floor(advancedPhase);
        if (completedCycles >=
            static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            return std::nullopt;
        }
        const auto cycleCount = static_cast<std::uint64_t>(completedCycles);
        if (config.waveform == LfoWaveform::SampleAndHold &&
            cycleCount > MaxSampleHoldCyclesPerUpdate) {
            return std::nullopt;
        }
        advancedPhases[lfo] = advancedPhase - completedCycles;
        completedCycleCounts[lfo] = cycleCount;
    }

    for (std::size_t lfo = 0; lfo < configs.size(); ++lfo) {
        const auto& config = configs[lfo];
        if (!config.enabled || elapsedSeconds == 0.0) {
            continue;
        }

        auto& runtime = runtimes_[track][lfo];
        if (config.waveform == LfoWaveform::SampleAndHold) {
            for (std::uint64_t cycle = 0;
                 cycle < completedCycleCounts[lfo];
                 ++cycle) {
                auto value = runtime.randomState;
                value ^= value << 13;
                value ^= value >> 17;
                value ^= value << 5;
                runtime.randomState = value == 0 ? 1 : value;
                runtime.heldValue =
                    (static_cast<double>(runtime.randomState) /
                     static_cast<double>(UINT32_MAX)) *
                        2.0 -
                    1.0;
            }
        }
        runtime.phase = advancedPhases[lfo];
        const double waveform = LfoWaveformGenerator::value(
            config.waveform, runtime.phase, runtime.heldValue);
        runtime.value = waveform;
        const auto destination =
            static_cast<std::size_t>(config.destination);
        contributions[destination] += waveform * config.amount;
    }

    return ModulationRouter::apply(
        base, contributions[0], contributions[1], contributions[2]);
}

std::optional<EffectiveRhythm> ModulationEngine::advance(
    std::size_t track,
    const LfoConfig& config,
    const EffectiveRhythm& base,
    double elapsedSeconds,
    double tempoBpm) noexcept {
    std::array<LfoConfig, MaxLfosPerTrack> configs{};
    configs[0] = config;
    return advance(track, configs, base, elapsedSeconds, tempoBpm);
}

LfoDisplayState ModulationEngine::display(std::size_t track,
                                          std::size_t lfo) const noexcept {
    LfoDisplayState state;
    if (track >= runtimes_.size() || lfo >= runtimes_[track].size()) {
        return state;
    }
    const auto& runtime = runtimes_[track][lfo];
    state.phase = std::clamp(runtime.phase, 0.0, 1.0);
    state.value = std::clamp(runtime.value, -1.0, 1.0);
    return state;
}

} // namespace pocket
