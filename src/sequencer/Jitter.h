#pragma once

#include "sequencer/EuclideanGenerator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace pocket {

enum class JitterMode : std::uint8_t {
    Insert,
    Drop,
    Shift,
    Mix
};

enum class JitterRate : std::uint8_t {
    Sixteenth,
    Eighth,
    Quarter,
    Half,
    Whole
};

struct JitterConfig {
    bool enabled = false;
    JitterMode mode = JitterMode::Mix;
    std::uint8_t intensity = 0;
    JitterRate rate = JitterRate::Sixteenth;
    std::uint8_t shiftRange = 1;
    bool useCustomSeed = false;
    std::uint32_t randomSeed = 1;
};

constexpr std::size_t JitterTrackCount = 8;
constexpr std::uint8_t MaxJitterShiftTicks = 8;

struct JitterRuntime {
    std::array<bool, EuclideanGenerator::MaxLength> events{};
    std::array<bool, EuclideanGenerator::MaxLength> dropSteps{};
    std::array<bool, EuclideanGenerator::MaxLength> insertSteps{};
    std::array<std::int8_t, EuclideanGenerator::MaxLength> shiftTicks{};
    std::uint32_t randomState = 1;
    std::uint32_t configuredSeed = 1;
    bool useCustomSeed = false;
    bool dirty = true;
};

class JitterEngine {
public:
    JitterEngine() noexcept;

    static bool isValid(const JitterConfig& config) noexcept;
    static std::uint8_t ticksPerInterval(JitterRate rate) noexcept;

    void reset(const std::array<JitterConfig, JitterTrackCount>& configs) noexcept;
    void resetTrack(std::size_t track, const JitterConfig& config) noexcept;
    void invalidate(std::size_t track) noexcept;
    std::optional<bool> process(std::size_t track,
                                const JitterConfig& config,
                                const EuclideanConfig& rhythm,
                                std::uint8_t step,
                                std::uint64_t masterTick) noexcept;

private:
    static std::uint32_t nextRandom(JitterRuntime& runtime) noexcept;
    static bool passesProbability(JitterRuntime& runtime,
                                  std::uint8_t intensity) noexcept;
    static void refreshDecisions(JitterRuntime& runtime,
                                 const JitterConfig& config) noexcept;
    static void buildEvents(JitterRuntime& runtime,
                            const JitterConfig& config,
                            const EuclideanConfig& rhythm) noexcept;

    std::array<JitterRuntime, JitterTrackCount> runtimes_{};
};

} // namespace pocket
