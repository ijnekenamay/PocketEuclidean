#pragma once

#include "sequencer/EuclideanGenerator.h"
#include "sequencer/Jitter.h"
#include "sequencer/Modulation.h"
#include "platform/TransportClock.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace pocket {

constexpr std::size_t TrackCount = 8;

enum class InstrumentType : std::uint8_t {
    Sampler,
    Synth
};

struct AdEnvelopeSettings {
    static constexpr float MinAttackSeconds = 0.001F;
    static constexpr float MaxAttackSeconds = 0.25F;
    static constexpr float MinDecaySeconds = 0.02F;
    static constexpr float MaxDecaySeconds = 4.0F;

    float attackSeconds = 0.005F;
    float decaySeconds = 0.45F;
};

struct TrackFilterSettings {
    static constexpr float MinCutoffHz = 100.0F;
    static constexpr float MaxCutoffHz = 20000.0F;

    bool enabled = false;
    float cutoffHz = 12000.0F;
};

struct TrackState {
    EuclideanConfig rhythm{};
    EffectiveRhythm effectiveRhythm{};
    std::array<LfoConfig, MaxLfosPerTrack> lfos{};
    JitterConfig jitter{};
    std::uint8_t position = 0;
    bool muted = false;
    InstrumentType instrument = InstrumentType::Sampler;
    std::uint8_t volume = 100;
    float pan = 0.0F;
    AdEnvelopeSettings envelope{};
    TrackFilterSettings filter{};
};

struct TickResult {
    std::array<bool, TrackCount> triggered{};
    std::array<std::uint8_t, TrackCount> stepPositions{};
    std::uint64_t masterTickPosition = 0;
    bool valid = true;
};

constexpr std::array<EuclideanConfig, TrackCount> ExamplePresetRhythms{{
    {16, 4, 0},
    {16, 3, 0},
    {12, 5, 0},
    {7, 3, 0},
    {15, 4, 0},
    {8, 5, 0},
    {11, 2, 0},
    {5, 2, 0},
}};

class Sequencer : public platform::ITransportClock {
public:
    static constexpr double MinTempoBpm = 40.0;
    static constexpr double MaxTempoBpm = 240.0;

    bool setTempo(double bpm) noexcept override;
    double tempo() const noexcept override { return tempoBpm_; }

    void start() noexcept override;
    void stop() noexcept override;
    void reset() noexcept;
    bool isPlaying() const noexcept override { return playing_; }
    std::uint64_t masterTickPosition() const noexcept override {
        return masterTickPosition_;
    }

    template <typename Callback>
    bool processFrames(std::uint32_t frameCount,
                       std::uint32_t sampleRate,
                       Callback&& onTick) {
        if (sampleRate == 0) {
            return false;
        }
        if (!playing_ || frameCount == 0) {
            return true;
        }

        const double blockDuration =
            static_cast<double>(frameCount) / static_cast<double>(sampleRate);
        const double secondsPerTick = 60.0 / (tempoBpm_ * 4.0);
        double elapsed = 0.0;

        while (secondsUntilNextTick_ < blockDuration - elapsed) {
            elapsed += secondsUntilNextTick_;
            const auto sampleOffset = static_cast<std::uint32_t>(
                std::ceil(elapsed * static_cast<double>(sampleRate) - 1.0e-9));
            onTick(sampleOffset, tick());
            secondsUntilNextTick_ = secondsPerTick;
        }

        secondsUntilNextTick_ -= blockDuration - elapsed;
        return true;
    }

    const TrackState& track(std::size_t index) const;
    bool setRhythm(std::size_t index, const EuclideanConfig& config);
    bool setLfo(std::size_t trackIndex,
                const LfoConfig& config,
                std::size_t lfoIndex = 0) noexcept;
    bool setJitter(std::size_t trackIndex,
                   const JitterConfig& config) noexcept;
    bool setMuted(std::size_t index, bool muted);
    bool setInstrument(std::size_t index, InstrumentType instrument);
    bool setVolume(std::size_t index, std::uint8_t volume);
    bool setPan(std::size_t index, float normalizedPan) noexcept;
    bool setEnvelope(std::size_t index,
                     const AdEnvelopeSettings& envelope) noexcept;
    bool setFilter(std::size_t index,
                   const TrackFilterSettings& filter) noexcept;

    TickResult tick();

private:
    std::array<TrackState, TrackCount> tracks_{};
    double tempoBpm_ = 120.0;
    double secondsUntilNextTick_ = 0.0;
    std::uint64_t masterTickPosition_ = 0;
    bool playing_ = false;
    ModulationEngine modulation_{};
    JitterEngine jitter_{};
};

} // namespace pocket
