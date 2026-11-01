#include "audio/RingsVoice.h"

#include "audio/MutableResampler.h"
#include "audio/PlaitsModels.h"

#include "rings/dsp/part.h"

#include <algorithm>
#include <array>

namespace pocket::audio {

namespace {

constexpr double CoreSampleRate = 48000.0;
constexpr double ActiveSeconds = 10.0;
constexpr std::size_t RingsBlock = rings::kMaxBlockSize;
constexpr float Position = 0.25F;

static_assert(RingsModelCount ==
                  static_cast<std::size_t>(rings::RESONATOR_MODEL_LAST),
              "RingsModelNames must match the resonator models");

} // namespace

struct RingsVoice::Impl {
    rings::Part part;
    std::array<std::uint16_t, 32768> reverbBuffer{};
    rings::PerformanceState performance{};
    rings::Patch patch{};
    std::array<float, RingsBlock> in{};
    std::array<float, RingsBlock> out{};
    std::array<float, RingsBlock> aux{};
    std::size_t cursor = RingsBlock;
    LinearStreamResampler resampler;

    SynthParameters parameters{};
    float velocity = 1.0F;
    bool active = false;
    bool strumPending = false;
    double activeSecondsLeft = 0.0;

    Impl() {
        part.Init(reverbBuffer.data());
        part.set_polyphony(1);
        part.set_model(rings::RESONATOR_MODEL_MODAL);
        performance.internal_exciter = true;
        performance.internal_strum = false;
        performance.internal_note = true;
        performance.note = 0.0F;
        performance.fm = 0.0F;
        patch.position = Position;
        resampler.reset();
    }

    void renderBlock() noexcept {
        const int models = static_cast<int>(RingsModelCount);
        part.set_model(static_cast<rings::ResonatorModel>(std::clamp(
            static_cast<int>(parameters.model), 0, models - 1)));
        performance.strum = strumPending;
        strumPending = false;
        performance.tonic = midiNoteFromPitchHz(parameters.pitchHz);
        performance.chord = std::clamp(
            static_cast<int>(parameters.color * rings::kNumChords), 0,
            static_cast<int>(rings::kNumChords) - 1);
        patch.structure = parameters.color;
        patch.brightness = parameters.timbre;
        patch.damping = parameters.morph;
        patch.position = Position;
        part.Process(performance, patch, in.data(), out.data(), aux.data(),
                     RingsBlock);
        cursor = 0;
    }

    float next() noexcept {
        if (cursor >= RingsBlock) {
            renderBlock();
        }
        const float sample = 0.5F * (out[cursor] + aux[cursor]);
        ++cursor;
        return sample;
    }
};

RingsVoice::RingsVoice() : impl_(std::make_unique<Impl>()) {}

RingsVoice::~RingsVoice() = default;

bool RingsVoice::setParameters(const SynthParameters& parameters) noexcept {
    if (!mutableParametersValid(parameters)) {
        return false;
    }
    impl_->parameters = parameters;
    return true;
}

bool RingsVoice::noteOn(float velocity) noexcept {
    if (!std::isfinite(velocity) || velocity <= 0.0F || velocity > 1.0F) {
        return false;
    }
    impl_->velocity = velocity;
    impl_->active = true;
    impl_->activeSecondsLeft = ActiveSeconds;
    impl_->strumPending = true;
    return true;
}

void RingsVoice::noteOff() noexcept {
    impl_->active = false;
    impl_->strumPending = false;
}

bool RingsVoice::isActive() const noexcept {
    return impl_->active;
}

bool RingsVoice::render(float* monoOutput,
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
