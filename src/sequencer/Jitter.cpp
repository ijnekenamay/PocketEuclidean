#include "sequencer/Jitter.h"

namespace pocket {
namespace {

constexpr std::array<std::uint8_t, 5> TicksPerInterval{{1, 2, 4, 8, 16}};

std::uint32_t seedForTrack(const JitterConfig& config,
                           std::size_t track) noexcept {
    const auto seed = config.useCustomSeed
                          ? config.randomSeed
                          : 0x9e3779b9U ^
                                static_cast<std::uint32_t>(
                                    track * 0x85ebca6bU);
    return seed == 0 ? 1 : seed;
}

} // namespace

JitterEngine::JitterEngine() noexcept {
    std::array<JitterConfig, JitterTrackCount> defaults{};
    reset(defaults);
}

bool JitterEngine::isValid(const JitterConfig& config) noexcept {
    return static_cast<std::uint8_t>(config.mode) <=
               static_cast<std::uint8_t>(JitterMode::Mix) &&
           static_cast<std::uint8_t>(config.rate) <
               static_cast<std::uint8_t>(JitterRate::Whole) + 1 &&
           config.intensity <= 100 &&
           config.shiftRange <= MaxJitterShiftTicks;
}

std::uint8_t JitterEngine::ticksPerInterval(JitterRate rate) noexcept {
    const auto index = static_cast<std::size_t>(rate);
    return index < TicksPerInterval.size() ? TicksPerInterval[index] : 0;
}

void JitterEngine::reset(
    const std::array<JitterConfig, JitterTrackCount>& configs) noexcept {
    for (std::size_t i = 0; i < runtimes_.size(); ++i) {
        resetTrack(i, configs[i]);
    }
}

void JitterEngine::resetTrack(std::size_t track,
                              const JitterConfig& config) noexcept {
    if (track < runtimes_.size()) {
        runtimes_[track] = {};
        runtimes_[track].randomState = seedForTrack(config, track);
        runtimes_[track].configuredSeed = config.randomSeed;
        runtimes_[track].useCustomSeed = config.useCustomSeed;
    }
}

void JitterEngine::invalidate(std::size_t track) noexcept {
    if (track < runtimes_.size()) {
        runtimes_[track].dirty = true;
    }
}

std::optional<bool> JitterEngine::process(
    std::size_t track,
    const JitterConfig& config,
    const EuclideanConfig& rhythm,
    std::uint8_t step,
    std::uint64_t masterTick) noexcept {
    if (track >= runtimes_.size() || !isValid(config) ||
        !EuclideanGenerator::isValid(rhythm) || step >= rhythm.length) {
        return std::nullopt;
    }
    const bool sourceEvent = EuclideanGenerator::isActive(rhythm, step);
    if (!config.enabled) {
        return sourceEvent;
    }

    auto& runtime = runtimes_[track];
    if (runtime.configuredSeed != config.randomSeed ||
        runtime.useCustomSeed != config.useCustomSeed) {
        runtime.randomState = seedForTrack(config, track);
        runtime.configuredSeed = config.randomSeed;
        runtime.useCustomSeed = config.useCustomSeed;
        runtime.dirty = true;
    }
    const auto interval = ticksPerInterval(config.rate);
    if (runtime.dirty || masterTick % interval == 0) {
        refreshDecisions(runtime, config);
    }
    buildEvents(runtime, config, rhythm);
    return runtime.events[step];
}

std::uint32_t JitterEngine::nextRandom(JitterRuntime& runtime) noexcept {
    auto value = runtime.randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    runtime.randomState = value == 0 ? 1 : value;
    return runtime.randomState;
}

bool JitterEngine::passesProbability(JitterRuntime& runtime,
                                     std::uint8_t intensity) noexcept {
    return intensity == 100 ||
           (intensity != 0 && nextRandom(runtime) % 100U < intensity);
}

void JitterEngine::refreshDecisions(JitterRuntime& runtime,
                                    const JitterConfig& config) noexcept {
    runtime.dropSteps.fill(false);
    runtime.insertSteps.fill(false);
    runtime.shiftTicks.fill(0);
    runtime.dirty = false;

    for (std::size_t step = 0; step < runtime.dropSteps.size(); ++step) {
        if (config.mode == JitterMode::Drop || config.mode == JitterMode::Mix) {
            runtime.dropSteps[step] =
                passesProbability(runtime, config.intensity);
        }
        if ((config.mode == JitterMode::Shift || config.mode == JitterMode::Mix) &&
            config.shiftRange != 0 &&
            passesProbability(runtime, config.intensity)) {
            const auto width = static_cast<std::uint32_t>(config.shiftRange) * 2U + 1U;
            runtime.shiftTicks[step] = static_cast<std::int8_t>(
                static_cast<int>(nextRandom(runtime) % width) -
                config.shiftRange);
        }
        if (config.mode == JitterMode::Insert || config.mode == JitterMode::Mix) {
            runtime.insertSteps[step] =
                passesProbability(runtime, config.intensity);
        }
    }
}

void JitterEngine::buildEvents(JitterRuntime& runtime,
                               const JitterConfig& config,
                               const EuclideanConfig& rhythm) noexcept {
    runtime.events.fill(false);
    std::array<bool, EuclideanGenerator::MaxLength> source{};
    for (std::uint8_t step = 0; step < rhythm.length; ++step) {
        source[step] = EuclideanGenerator::isActive(rhythm, step);
    }

    for (std::uint8_t sourceStep = 0; sourceStep < rhythm.length; ++sourceStep) {
        if (!source[sourceStep] || runtime.dropSteps[sourceStep]) {
            continue;
        }
        const int displacement =
            config.mode == JitterMode::Shift || config.mode == JitterMode::Mix
                ? runtime.shiftTicks[sourceStep]
                : 0;
        const int destination =
            ((static_cast<int>(sourceStep) + displacement) % rhythm.length +
             rhythm.length) %
            rhythm.length;
        if (!runtime.events[static_cast<std::size_t>(destination)]) {
            runtime.events[static_cast<std::size_t>(destination)] = true;
        }
    }

    if (config.mode == JitterMode::Insert || config.mode == JitterMode::Mix) {
        for (std::uint8_t step = 0; step < rhythm.length; ++step) {
            if (!source[step] && !runtime.events[step] &&
                runtime.insertSteps[step]) {
                runtime.events[step] = true;
            }
        }
    }
}

} // namespace pocket
