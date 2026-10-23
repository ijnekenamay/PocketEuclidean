#pragma once

#include <cstdint>

namespace pocket::platform {

class ITransportClock {
public:
    virtual ~ITransportClock() = default;
    virtual bool setTempo(double bpm) noexcept = 0;
    virtual double tempo() const noexcept = 0;
    virtual bool isPlaying() const noexcept = 0;
    virtual std::uint64_t masterTickPosition() const noexcept = 0;
    virtual void start() noexcept = 0;
    virtual void stop() noexcept = 0;
};

} // namespace pocket::platform
