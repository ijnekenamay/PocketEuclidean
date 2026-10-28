#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace pocket {

// S&H phrase generator inspired by the classic "Turing Machine" module.
// Each track owns a register of 32 bytes. Every clock reads the byte at the
// head, turns it into a scale degree, then optionally re-randomises that byte
// so a low CHANCE locks a loop and a high CHANCE keeps it evolving.
enum class TuringClockMode : std::uint8_t {
    Tempo,
    Euclid,
    Free
};

enum class TuringRate : std::uint8_t {
    Sixteenth,
    Eighth,
    Quarter,
    Half,
    Whole,
    Count
};

enum class TuringScale : std::uint8_t {
    Chromatic,
    Major,
    Minor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    MajorPentatonic,
    MinorPentatonic,
    Count
};

struct TuringConfig {
    bool enabled = false;
    TuringClockMode clock = TuringClockMode::Tempo;
    TuringRate rate = TuringRate::Sixteenth;
    std::uint8_t length = 8;
    std::uint8_t chance = 50;
    std::uint8_t range = 7;
    TuringScale scale = TuringScale::Major;
    std::uint8_t root = 0;
    std::uint8_t glide = 0;
    std::uint8_t drift = 0;
    // Performance lock: freezes the register and the drift so the current
    // phrase repeats exactly, even while CHANCE and DRIFT are above zero.
    // Rolling a new seed still rewrites it, which is the "unlock" gesture.
    bool locked = false;
    bool useCustomSeed = false;
    std::uint32_t randomSeed = 1;
};

constexpr std::size_t TuringTrackCount = 8;
constexpr std::size_t TuringRegisterLength = 32;
constexpr std::int8_t TuringMaxSemitones = 24;

struct TuringDisplayState {
    std::array<std::uint8_t, TuringRegisterLength> values{};
    std::uint8_t head = 0;
    std::uint8_t length = 0;
    std::int8_t semitone = 0;
    bool enabled = false;
    bool locked = false;
    bool valid = false;
};

class TuringScaleMap {
public:
    static const char* name(TuringScale scale) noexcept;
    static int noteCount(TuringScale scale) noexcept;
    // Scale degree -> semitone offset. Degrees below zero wrap down an octave.
    static int semitones(TuringScale scale, int degree) noexcept;
    static const char* noteName(std::uint8_t root) noexcept;
};

class TuringEngine {
public:
    TuringEngine() noexcept;

    static bool isValid(const TuringConfig& config) noexcept;
    static std::uint8_t ticksPerStep(TuringRate rate) noexcept;
    static std::uint8_t euclidTriggersPerStep(TuringRate rate) noexcept;
    static double freeRateHz(TuringRate rate) noexcept;

    void reset(const std::array<TuringConfig, TuringTrackCount>& configs) noexcept;
    void resetTrack(std::size_t track, const TuringConfig& config) noexcept;
    // Keeps the register and seed, but rewinds the loop for a fresh transport.
    void resetTransport() noexcept;
    // Performance gesture: refill the register from the running PRNG.
    void reseed(std::size_t track, const TuringConfig& config) noexcept;
    // Re-clamps the head after a length edit and refreshes the live readout.
    void configure(std::size_t track, const TuringConfig& config) noexcept;

    // Called once per sequencer tick. `euclidTriggered` is the post-jitter
    // trigger for this track, so EUCLID mode follows the audible rhythm.
    // Returns the semitone offset to apply to the track, or nullopt when the
    // configuration is invalid.
    std::optional<std::int8_t> process(std::size_t track,
                                       const TuringConfig& config,
                                       bool euclidTriggered,
                                       std::uint64_t masterTick,
                                       double elapsedSeconds) noexcept;

    TuringDisplayState display(std::size_t track,
                               const TuringConfig& config) const noexcept;

private:
    struct Runtime {
        std::array<std::uint8_t, TuringRegisterLength> values{};
        std::uint32_t randomState = 1;
        std::uint32_t configuredSeed = 1;
        bool useCustomSeed = false;
        bool seeded = false;
        std::uint8_t head = 0;
        // Wide enough that the modulo never wraps back mid-phrase.
        std::uint32_t triggerCounter = 0;
        double freePhase = 0.0;
        std::int8_t driftSemitone = 0;
        std::int8_t currentSemitone = 0;
    };

    static std::uint32_t seedForTrack(const TuringConfig& config,
                                      std::size_t track) noexcept;
    static std::uint32_t nextRandom(Runtime& runtime) noexcept;
    static std::uint8_t randomByte(Runtime& runtime) noexcept;
    static int mapValue(std::uint8_t value,
                        const TuringConfig& config) noexcept;
    static void updateDrift(Runtime& runtime,
                            const TuringConfig& config) noexcept;

    std::array<Runtime, TuringTrackCount> runtimes_{};
};

} // namespace pocket
