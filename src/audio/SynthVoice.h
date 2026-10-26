#pragma once

#include "sequencer/Sequencer.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket::audio {

struct SynthParameters {
    float pitchHz = 110.0F;
    float timbre = 0.5F;
    float color = 0.5F;
    float morph = 0.5F;
    float level = 0.5F;
    SynthEngineType engine = SynthEngineType::BasicVa;
    // Plaits model index (0..PlaitsModelCount-1). Independent of morph, which
    // keeps its continuous per-model role.
    std::uint8_t model = 0;
};

class ISynthVoice {
public:
    virtual ~ISynthVoice() = default;

    virtual bool setParameters(const SynthParameters& parameters) noexcept = 0;
    virtual bool noteOn(float velocity) noexcept = 0;
    virtual void noteOff() noexcept = 0;
    // Must fill the complete mono output block without allocation or blocking.
    virtual bool render(float* monoOutput,
                        std::size_t frameCount,
                        std::uint32_t sampleRate) noexcept = 0;
    virtual bool isRealtimeSafe() const noexcept = 0;
};

class SynthVoiceRack {
public:
    static constexpr std::size_t MaxVoices = TrackCount;
    static constexpr std::size_t MaxRenderFrames = 4096;
    static constexpr float MaxPitchHz = 20000.0F;

    // Setup-only; configure all track voices before opening the audio device.
    bool configure(std::size_t track,
                   ISynthVoice* voice,
                   const SynthParameters& parameters) noexcept;
    bool updateParameters(std::size_t track,
                          const SynthParameters& parameters) noexcept;
    const SynthParameters& parameters(std::size_t track) const noexcept;
    bool trigger(std::size_t track, float velocity = 1.0F) noexcept;
    bool renderTrack(std::size_t track,
                     float* monoOutput,
                     std::size_t frameCount,
                     std::uint32_t sampleRate) noexcept;
    void stopTrack(std::size_t track) noexcept;
    void stopAll() noexcept;

    bool isConfigured(std::size_t track) const noexcept;
    bool isTrackRealtimeSafe(std::size_t track) const noexcept;
    bool isRealtimeSafe() const noexcept;

private:
    static bool isValid(const SynthParameters& parameters) noexcept;

    std::array<ISynthVoice*, MaxVoices> voices_{};
    std::array<SynthParameters, MaxVoices> parameters_{};
};

} // namespace pocket::audio
