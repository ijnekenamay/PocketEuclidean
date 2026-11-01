#include "audio/PlaitsVoice.h"

#include "audio/MutableResampler.h"
#include "audio/PlaitsModels.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"
#include "stmlib/utils/buffer_allocator.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace pocket::audio {

namespace {

constexpr double CoreSampleRate = 48000.0;
// Engines are idle-skipped this long after the last trigger; the longest
// Plaits decay tail is well under this.
constexpr double ActiveSeconds = 8.0;
// The trigger input must stay high long enough to be seen through Plaits'
// trigger delay line, then drop so the next note produces a fresh edge.
constexpr int GateBlocks = 16;

} // namespace

struct PlaitsVoice::Impl {
    plaits::Voice voice;
    alignas(16) std::array<std::uint8_t, 32768> sharedBuffer{};
    plaits::Patch patch{};
    plaits::Modulations modulations{};
    std::array<plaits::Voice::Frame, plaits::kBlockSize> frames{};
    std::size_t cursor = plaits::kBlockSize;
    LinearStreamResampler resampler;

    SynthParameters parameters{};
    float velocity = 1.0F;
    bool active = false;
    double activeSecondsLeft = 0.0;

    bool triggerPending = false;
    bool gateHigh = false;
    int gateBlocksLeft = 0;

    Impl() {
        stmlib::BufferAllocator allocator(sharedBuffer.data(),
                                          sharedBuffer.size());
        voice.Init(&allocator);

        patch.note = 48.0F;
        patch.harmonics = 0.5F;
        patch.timbre = 0.5F;
        patch.morph = 0.5F;
        patch.frequency_modulation_amount = 0.0F;
        patch.timbre_modulation_amount = 0.0F;
        patch.morph_modulation_amount = 0.0F;
        patch.engine = 0;
        patch.decay = 0.5F;
        patch.lpg_colour = 0.5F;

        modulations.engine = 0.0F;
        modulations.note = 0.0F;
        modulations.frequency = 0.0F;
        modulations.harmonics = 0.0F;
        modulations.timbre = 0.0F;
        modulations.morph = 0.0F;
        modulations.trigger = 0.0F;
        modulations.level = 0.0F;
        modulations.frequency_patched = false;
        modulations.timbre_patched = false;
        modulations.morph_patched = false;
        modulations.trigger_patched = true;
        modulations.level_patched = false;
        resampler.reset();
    }

    void stepGate() noexcept {
        if (triggerPending) {
            if (gateHigh) {
                gateHigh = false;
                gateBlocksLeft = 0;
            } else {
                gateHigh = true;
                gateBlocksLeft = GateBlocks;
                triggerPending = false;
            }
        } else if (gateHigh && --gateBlocksLeft <= 0) {
            gateHigh = false;
        }
        modulations.trigger = gateHigh ? 1.0F : 0.0F;
    }

    void renderBlock() noexcept {
        stepGate();
        const int models = static_cast<int>(PlaitsModelCount);
        patch.engine = std::clamp(static_cast<int>(parameters.model), 0,
                                  models - 1);
        patch.note = midiNoteFromPitchHz(parameters.pitchHz);
        patch.harmonics = parameters.color;
        patch.timbre = parameters.timbre;
        patch.morph = parameters.morph;
        voice.Render(patch, modulations, frames.data(), plaits::kBlockSize);
        cursor = 0;
    }

    float next() noexcept {
        if (cursor >= plaits::kBlockSize) {
            renderBlock();
        }
        return static_cast<float>(frames[cursor++].out) * (1.0F / 32768.0F);
    }
};

PlaitsVoice::PlaitsVoice() : impl_(std::make_unique<Impl>()) {}

PlaitsVoice::~PlaitsVoice() = default;

bool PlaitsVoice::setParameters(const SynthParameters& parameters) noexcept {
    if (!mutableParametersValid(parameters)) {
        return false;
    }
    impl_->parameters = parameters;
    return true;
}

bool PlaitsVoice::noteOn(float velocity) noexcept {
    if (!std::isfinite(velocity) || velocity <= 0.0F || velocity > 1.0F) {
        return false;
    }
    impl_->velocity = velocity;
    impl_->active = true;
    impl_->activeSecondsLeft = ActiveSeconds;
    impl_->triggerPending = true;
    return true;
}

void PlaitsVoice::noteOff() noexcept {
    impl_->active = false;
    impl_->triggerPending = false;
    impl_->gateHigh = false;
    impl_->gateBlocksLeft = 0;
    impl_->modulations.trigger = 0.0F;
}

bool PlaitsVoice::isActive() const noexcept {
    return impl_->active;
}

bool PlaitsVoice::render(float* monoOutput,
                         std::size_t frameCount,
                         std::uint32_t sampleRate) noexcept {
    if (monoOutput == nullptr ||
        frameCount > SynthVoiceRack::MaxRenderFrames || sampleRate < 8000 ||
        sampleRate > 192000) {
        return false;
    }
    auto& impl = *impl_;
    if (!impl.active) {
        std::fill(monoOutput, monoOutput + frameCount, 0.0F);
        return true;
    }

    impl.resampler.render(monoOutput, frameCount, CoreSampleRate,
                          static_cast<double>(sampleRate), impl);
    const float gain = impl.velocity * impl.parameters.level;
    for (std::size_t i = 0; i < frameCount; ++i) {
        monoOutput[i] = std::clamp(monoOutput[i] * gain, -1.0F, 1.0F);
    }

    impl.activeSecondsLeft -=
        static_cast<double>(frameCount) / static_cast<double>(sampleRate);
    if (impl.activeSecondsLeft <= 0.0) {
        impl.active = false;
    }
    return true;
}

} // namespace pocket::audio
