#pragma once

#include "sequencer/Sequencer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pocket::audio {

enum class AudioParameter : std::uint8_t {
    TrackVolume,
    TrackPan,
    TrackMute,
    TrackAttack,
    TrackDecay,
    TrackFilterEnabled,
    TrackFilterCutoff,
    MasterVolume
};

struct AudioParameterCommand {
    AudioParameter parameter = AudioParameter::TrackVolume;
    std::uint8_t track = 0;
    float value = 0.0F;
};

class AudioParameterQueue {
public:
    static constexpr std::size_t Capacity = 64;

    bool push(const AudioParameterCommand& command) noexcept;
    bool pop(AudioParameterCommand& command) noexcept;
    bool isLockFree() const noexcept;

    static bool isValid(const AudioParameterCommand& command) noexcept;

private:
    std::array<AudioParameterCommand, Capacity> commands_{};
    alignas(64) std::atomic<std::uint32_t> writeIndex_{0};
    alignas(64) std::atomic<std::uint32_t> readIndex_{0};
};

} // namespace pocket::audio
