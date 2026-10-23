#pragma once

#include "sequencer/Sequencer.h"
#include "ui/InputMapping.h"

#include <cstdint>

namespace pocket::platform {

struct MidiEvent {
    enum class Type : std::uint8_t {
        Clock,
        Start,
        Stop,
        Continue,
        NoteOn,
        NoteOff,
        ControlChange
    };

    Type type = Type::Clock;
    std::uint8_t channel = 1;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

struct AudioDeviceSpec {
    std::uint32_t sampleRate = 48000;
    std::uint16_t bufferFrames = 256;
};

constexpr bool isValid(const AudioDeviceSpec& spec) noexcept {
    return spec.sampleRate >= 8000 && spec.sampleRate <= 192000 &&
           spec.bufferFrames >= 32 && spec.bufferFrames <= 4096;
}

using AudioCallback = void (*)(void* userData,
                               float* interleavedStereo,
                               std::uint32_t frameCount,
                               std::uint64_t blockStartFrame,
                               std::uint32_t sampleRate) noexcept;

// Callback data is interleaved float stereo; blockStartFrame is monotonic
// from zero for each successful open. The callback must fill every frame.
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    virtual bool open(const AudioDeviceSpec& requested,
                      AudioCallback callback,
                      void* userData,
                      AudioDeviceSpec& obtained) noexcept = 0;
    virtual void close() noexcept = 0;
};

class IInputBackend {
public:
    virtual ~IInputBackend() = default;
    virtual bool poll(ui::ActionEvent& action) noexcept = 0;
};

class IMidiBackend {
public:
    virtual ~IMidiBackend() = default;
    virtual bool open() noexcept = 0;
    virtual void close() noexcept = 0;
    virtual bool poll(MidiEvent& event) noexcept = 0;
    virtual bool send(const MidiEvent& event) noexcept = 0;
};

class ITriggerSink {
public:
    virtual ~ITriggerSink() = default;
    virtual void onTick(const TickResult& event) noexcept = 0;
};

} // namespace pocket::platform
