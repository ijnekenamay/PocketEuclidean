#pragma once

#include "audio/BasicVaVoice.h"
#include "audio/PlaitsVoice.h"
#include "audio/SynthVoice.h"

#include <cstddef>
#include <cstdint>

namespace pocket::audio {

// Composite backend that picks one of the built-in engines from the engine
// type carried in SynthParameters.
//
// SynthVoiceRack::configure is setup-only, so the rack slot is bound to this
// object once before the audio device opens. Later engine changes arrive as
// ordinary parameter updates on the audio thread (AudioParameter::
// TrackSynthEngineType), which keeps the switch allocation-free and avoids
// mutating the rack while the callback is rendering.
class SelectableSynthVoice final : public ISynthVoice {
public:
    bool setParameters(const SynthParameters& parameters) noexcept override {
        if (!basic_.setParameters(parameters) ||
            !plaits_.setParameters(parameters)) {
            return false;
        }
        parameters_ = parameters;
        return true;
    }

    bool noteOn(float velocity) noexcept override {
        return activeVoice().noteOn(velocity);
    }

    void noteOff() noexcept override {
        basic_.noteOff();
        plaits_.noteOff();
    }

    bool render(float* monoOutput,
                std::size_t frameCount,
                std::uint32_t sampleRate) noexcept override {
        return activeVoice().render(monoOutput, frameCount, sampleRate);
    }

    bool isRealtimeSafe() const noexcept override {
        return basic_.isRealtimeSafe() && plaits_.isRealtimeSafe();
    }

    SynthEngineType engineType() const noexcept { return parameters_.engine; }

private:
    ISynthVoice& activeVoice() noexcept {
        return parameters_.engine == SynthEngineType::Plaits
                   ? static_cast<ISynthVoice&>(plaits_)
                   : static_cast<ISynthVoice&>(basic_);
    }

    BasicVaVoice basic_{};
    PlaitsVoice plaits_{};
    SynthParameters parameters_{};
};

} // namespace pocket::audio
