#include "sequencer/Turing.h"

#include <algorithm>
#include <cmath>

namespace pocket {
namespace {

constexpr std::array<std::uint8_t, 5> TicksPerStep{{1, 2, 4, 8, 16}};
constexpr std::array<std::uint8_t, 5> EuclidTriggersPerStep{{1, 2, 3, 4, 6}};
constexpr std::array<double, 5> FreeRatesHz{{0.5, 1.0, 2.0, 4.0, 8.0}};

constexpr std::array<int, 7> Major{{0, 2, 4, 5, 7, 9, 11}};
constexpr std::array<int, 7> Minor{{0, 2, 3, 5, 7, 8, 10}};
constexpr std::array<int, 7> Dorian{{0, 2, 3, 5, 7, 9, 10}};
constexpr std::array<int, 7> Phrygian{{0, 1, 3, 5, 7, 8, 10}};
constexpr std::array<int, 7> Lydian{{0, 2, 4, 6, 7, 9, 11}};
constexpr std::array<int, 7> Mixolydian{{0, 2, 4, 5, 7, 9, 10}};
constexpr std::array<int, 5> MajorPentatonic{{0, 2, 4, 7, 9}};
constexpr std::array<int, 5> MinorPentatonic{{0, 3, 5, 7, 10}};

} // namespace

const char* TuringScaleMap::name(TuringScale scale) noexcept {
    switch (scale) {
    case TuringScale::Chromatic: return "CHROMATIC";
    case TuringScale::Major: return "MAJOR";
    case TuringScale::Minor: return "MINOR";
    case TuringScale::Dorian: return "DORIAN";
    case TuringScale::Phrygian: return "PHRYGIAN";
    case TuringScale::Lydian: return "LYDIAN";
    case TuringScale::Mixolydian: return "MIXOLYDIAN";
    case TuringScale::MajorPentatonic: return "PENTA MAJ";
    case TuringScale::MinorPentatonic: return "PENTA MIN";
    case TuringScale::Count: break;
    }
    return "?";
}

int TuringScaleMap::noteCount(TuringScale scale) noexcept {
    switch (scale) {
    case TuringScale::Chromatic: return 12;
    case TuringScale::MajorPentatonic:
    case TuringScale::MinorPentatonic:
        return 5;
    default:
        return 7;
    }
}

int TuringScaleMap::semitones(TuringScale scale, int degree) noexcept {
    if (scale == TuringScale::Chromatic) {
        return degree;
    }
    const int count = noteCount(scale);
    if (count <= 0) {
        return degree;
    }
    int octave = degree / count;
    int index = degree % count;
    if (index < 0) {
        index += count;
        --octave;
    }
    int semitone = 0;
    switch (scale) {
    case TuringScale::Major: semitone = Major[static_cast<std::size_t>(index)]; break;
    case TuringScale::Minor: semitone = Minor[static_cast<std::size_t>(index)]; break;
    case TuringScale::Dorian: semitone = Dorian[static_cast<std::size_t>(index)]; break;
    case TuringScale::Phrygian: semitone = Phrygian[static_cast<std::size_t>(index)]; break;
    case TuringScale::Lydian: semitone = Lydian[static_cast<std::size_t>(index)]; break;
    case TuringScale::Mixolydian: semitone = Mixolydian[static_cast<std::size_t>(index)]; break;
    case TuringScale::MajorPentatonic:
        semitone = MajorPentatonic[static_cast<std::size_t>(index)];
        break;
    case TuringScale::MinorPentatonic:
        semitone = MinorPentatonic[static_cast<std::size_t>(index)];
        break;
    default:
        break;
    }
    return octave * 12 + semitone;
}

const char* TuringScaleMap::noteName(std::uint8_t root) noexcept {
    static constexpr std::array<const char*, 12> Names{{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};
    return Names[root % Names.size()];
}

TuringEngine::TuringEngine() noexcept {
    std::array<TuringConfig, TuringTrackCount> defaults{};
    reset(defaults);
}

bool TuringEngine::isValid(const TuringConfig& config) noexcept {
    return static_cast<std::uint8_t>(config.clock) <=
               static_cast<std::uint8_t>(TuringClockMode::Free) &&
           static_cast<std::uint8_t>(config.rate) <
               static_cast<std::uint8_t>(TuringRate::Count) &&
           config.length >= 2 && config.length <= TuringRegisterLength &&
           config.chance <= 100 && config.range >= 1 && config.range <= 24 &&
           static_cast<std::uint8_t>(config.scale) <
               static_cast<std::uint8_t>(TuringScale::Count) &&
           config.root <= 11 && config.glide <= 100 && config.drift <= 100;
}

std::uint8_t TuringEngine::ticksPerStep(TuringRate rate) noexcept {
    const auto index = static_cast<std::size_t>(rate);
    return index < TicksPerStep.size() ? TicksPerStep[index] : 0;
}

std::uint8_t TuringEngine::euclidTriggersPerStep(TuringRate rate) noexcept {
    const auto index = static_cast<std::size_t>(rate);
    return index < EuclidTriggersPerStep.size()
               ? EuclidTriggersPerStep[index]
               : 0;
}

double TuringEngine::freeRateHz(TuringRate rate) noexcept {
    const auto index = static_cast<std::size_t>(rate);
    return index < FreeRatesHz.size() ? FreeRatesHz[index] : 0.0;
}

void TuringEngine::reset(
    const std::array<TuringConfig, TuringTrackCount>& configs) noexcept {
    for (std::size_t track = 0; track < runtimes_.size(); ++track) {
        resetTrack(track, configs[track]);
    }
}

void TuringEngine::resetTrack(std::size_t track,
                              const TuringConfig& config) noexcept {
    if (track >= runtimes_.size()) {
        return;
    }
    auto& runtime = runtimes_[track];
    runtime = {};
    runtime.randomState = seedForTrack(config, track);
    runtime.configuredSeed = config.randomSeed;
    runtime.useCustomSeed = config.useCustomSeed;
    for (auto& value : runtime.values) {
        value = randomByte(runtime);
    }
    runtime.seeded = true;
    runtime.currentSemitone = static_cast<std::int8_t>(
        std::clamp(mapValue(runtime.values[0], config),
                   -static_cast<int>(TuringMaxSemitones),
                   static_cast<int>(TuringMaxSemitones)));
}

void TuringEngine::resetTransport() noexcept {
    for (auto& runtime : runtimes_) {
        runtime.head = 0;
        runtime.triggerCounter = 0;
        runtime.freePhase = 0.0;
        runtime.driftSemitone = 0;
        runtime.currentSemitone = 0;
    }
}

void TuringEngine::reseed(std::size_t track,
                          const TuringConfig& config) noexcept {
    if (track >= runtimes_.size()) {
        return;
    }
    auto& runtime = runtimes_[track];
    for (auto& value : runtime.values) {
        value = randomByte(runtime);
    }
    runtime.head = 0;
    runtime.driftSemitone = 0;
    runtime.currentSemitone = static_cast<std::int8_t>(
        std::clamp(mapValue(runtime.values[0], config),
                   -static_cast<int>(TuringMaxSemitones),
                   static_cast<int>(TuringMaxSemitones)));
}

void TuringEngine::configure(std::size_t track,
                             const TuringConfig& config) noexcept {
    if (track >= runtimes_.size() || !isValid(config)) {
        return;
    }
    auto& runtime = runtimes_[track];
    if (!runtime.seeded) {
        resetTrack(track, config);
        return;
    }
    runtime.head %= config.length;
    runtime.currentSemitone = static_cast<std::int8_t>(
        std::clamp(mapValue(runtime.values[runtime.head], config),
                   -static_cast<int>(TuringMaxSemitones),
                   static_cast<int>(TuringMaxSemitones)));
}

std::optional<std::int8_t> TuringEngine::process(
    std::size_t track,
    const TuringConfig& config,
    bool euclidTriggered,
    std::uint64_t masterTick,
    double elapsedSeconds) noexcept {
    if (track >= runtimes_.size() || !isValid(config)) {
        return std::nullopt;
    }

    auto& runtime = runtimes_[track];
    if (!runtime.seeded || runtime.configuredSeed != config.randomSeed ||
        runtime.useCustomSeed != config.useCustomSeed) {
        resetTrack(track, config);
    }
    if (!config.enabled) {
        runtime.currentSemitone = 0;
        return static_cast<std::int8_t>(0);
    }

    bool clocked = false;
    switch (config.clock) {
    case TuringClockMode::Tempo: {
        const auto interval = ticksPerStep(config.rate);
        clocked = interval != 0 && masterTick % interval == 0;
        break;
    }
    case TuringClockMode::Euclid:
        if (euclidTriggered) {
            ++runtime.triggerCounter;
            const auto interval = euclidTriggersPerStep(config.rate);
            clocked = interval != 0 &&
                      runtime.triggerCounter % interval == 0;
        }
        break;
    case TuringClockMode::Free: {
        const double elapsed =
            std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0
                ? elapsedSeconds
                : 0.0;
        runtime.freePhase += elapsed * freeRateHz(config.rate);
        if (runtime.freePhase >= 1.0) {
            runtime.freePhase -= std::floor(runtime.freePhase);
            clocked = true;
        }
        break;
    }
    }

    // Capture the drift before a loop-end update so the note just read keeps
    // the transposition it was chosen with.
    const std::int8_t noteDrift = runtime.driftSemitone;
    if (clocked) {
        const auto value = runtime.values[runtime.head];
        runtime.currentSemitone = static_cast<std::int8_t>(
            std::clamp(mapValue(value, config),
                       -static_cast<int>(TuringMaxSemitones),
                       static_cast<int>(TuringMaxSemitones)));
        if (!config.locked &&
            (config.chance == 100 ||
             (config.chance != 0 &&
              nextRandom(runtime) % 100U < config.chance))) {
            runtime.values[runtime.head] = randomByte(runtime);
        }
        runtime.head =
            static_cast<std::uint8_t>((runtime.head + 1) % config.length);
        if (runtime.head == 0 && !config.locked) {
            // The new drift starts with the next loop.
            updateDrift(runtime, config);
        }
    }

    const int offset = static_cast<int>(runtime.currentSemitone) +
                       static_cast<int>(noteDrift);
    return static_cast<std::int8_t>(
        std::clamp(offset, -static_cast<int>(TuringMaxSemitones),
                   static_cast<int>(TuringMaxSemitones)));
}

TuringDisplayState TuringEngine::display(
    std::size_t track, const TuringConfig& config) const noexcept {
    TuringDisplayState state;
    if (track >= runtimes_.size()) {
        return state;
    }
    const auto& runtime = runtimes_[track];
    state.values = runtime.values;
    state.head = config.length > 0 ? runtime.head % config.length : 0;
    state.length = config.length;
    state.semitone = static_cast<std::int8_t>(
        std::clamp(static_cast<int>(runtime.currentSemitone) +
                       static_cast<int>(runtime.driftSemitone),
                   -static_cast<int>(TuringMaxSemitones),
                   static_cast<int>(TuringMaxSemitones)));
    state.enabled = config.enabled;
    state.locked = config.locked;
    state.valid = runtime.seeded;
    return state;
}

std::uint32_t TuringEngine::seedForTrack(const TuringConfig& config,
                                         std::size_t track) noexcept {
    const auto seed = config.useCustomSeed
                          ? config.randomSeed
                          : 0x9e3779b9U ^
                                static_cast<std::uint32_t>(
                                    track * 0x85ebca6bU);
    return seed == 0 ? 1 : seed;
}

std::uint32_t TuringEngine::nextRandom(Runtime& runtime) noexcept {
    auto value = runtime.randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    runtime.randomState = value == 0 ? 1 : value;
    return runtime.randomState;
}

std::uint8_t TuringEngine::randomByte(Runtime& runtime) noexcept {
    return static_cast<std::uint8_t>(nextRandom(runtime) >> 24);
}

int TuringEngine::mapValue(std::uint8_t value,
                           const TuringConfig& config) noexcept {
    const double normalized =
        (static_cast<double>(value) - 127.5) / 127.5;
    const int degree = static_cast<int>(
        std::lround(normalized * static_cast<double>(config.range)));
    const int semitone =
        TuringScaleMap::semitones(config.scale, degree) +
        static_cast<int>(config.root);
    return std::clamp(semitone, -static_cast<int>(TuringMaxSemitones),
                      static_cast<int>(TuringMaxSemitones));
}

void TuringEngine::updateDrift(Runtime& runtime,
                               const TuringConfig& config) noexcept {
    const int maxDrift = static_cast<int>(config.drift) * 12 / 100;
    if (maxDrift <= 0) {
        runtime.driftSemitone = 0;
        return;
    }
    const int step = (nextRandom(runtime) & 1U) != 0U ? 1 : -1;
    runtime.driftSemitone = static_cast<std::int8_t>(
        std::clamp(static_cast<int>(runtime.driftSemitone) + step,
                   -maxDrift, maxDrift));
}

} // namespace pocket
