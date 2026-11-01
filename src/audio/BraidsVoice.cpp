#include "audio/BraidsVoice.h"

#include "audio/MutableResampler.h"
#include "audio/PlaitsModels.h"

#include "braids/macro_oscillator.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace pocket::audio {

namespace {

constexpr double CoreSampleRate = 48000.0;
constexpr std::size_t BraidsBlock = 24;
// Braids pitch is in 1/128 semitone, 0..16383 (128 semitones).
constexpr float PitchUnitsPerSemitone = 128.0F;
constexpr float MaxPitchUnits = 16383.0F;
// Leaves headroom for the track envelope, filter and mixer.
constexpr float OutputScale = 0.5F / 32768.0F;

static_assert(BraidsModelCount ==
                  static_cast<std::size_t>(
                      braids::MACRO_OSC_SHAPE_LAST_ACCESSIBLE_FROM_META) + 1,
              "BraidsModelNames must match the macro-oscillator shapes");

} // namespace

struct BraidsVoice::Impl {
    braids::MacroOscillator oscillator;
    std::array<std::uint8_t, BraidsBlock> sync{};
    std::array<std::int16_t, BraidsBlock> block{};
    std::size_t cursor = BraidsBlock;
    float previous = 0.0F;
    LinearStreamResampler resampler;

    SynthParameters parameters{};
    float velocity = 1.0F;
    bool active = false;

    Impl() {
        oscillator.Init();
        oscillator.set_shape(braids::MACRO_OSC_SHAPE_CSAW);
        resampler.reset();
    }

    float next96() noexcept {
        if (cursor >= BraidsBlock) {
            const int shapes = static_cast<int>(BraidsModelCount);
            oscillator.set_shape(static_cast<braids::MacroOscillatorShape>(
                std::clamp(static_cast<int>(parameters.model), 0,
                           shapes - 1)));
            const float pitch =
                midiNoteFromPitchHz(parameters.pitchHz) * PitchUnitsPerSemitone;
            oscillator.set_pitch(static_cast<std::int16_t>(
                std::clamp(pitch, 0.0F, MaxPitchUnits)));
            oscillator.set_parameters(
                static_cast<std::int16_t>(parameters.timbre * 32767.0F),
                static_cast<std::int16_t>(parameters.color * 32767.0F));
            oscillator.Render(sync.data(), block.data(), BraidsBlock);
            cursor = 0;
        }
        return static_cast<float>(block[cursor++]) * OutputScale;
    }

    // [1 2 1] / 4 low-pass, keeping every second sample (96 kHz -> 48 kHz).
    float next() noexcept {
        const float a = next96();
        const float b = next96();
        const float out = 0.25F * previous + 0.5F * a + 0.25F * b;
        previous = b;
        return out;
    }
};

BraidsVoice::BraidsVoice() : impl_(std::make_unique<Impl>()) {}

BraidsVoice::~BraidsVoice() = default;

bool BraidsVoice::setParameters(const SynthParameters& parameters) noexcept {
    if (!mutableParametersValid(parameters)) {
        return false;
    }
    impl_->parameters = parameters;
    return true;
}

bool BraidsVoice::noteOn(float velocity) noexcept {
    if (!std::isfinite(velocity) || velocity <= 0.0F || velocity > 1.0F) {
        return false;
    }
    impl_->velocity = velocity;
    impl_->active = true;
    impl_->oscillator.Strike();
    return true;
}

void BraidsVoice::noteOff() noexcept {
    impl_->active = false;
}

bool BraidsVoice::isActive() const noexcept {
    return impl_->active;
}

bool BraidsVoice::render(float* monoOutput,
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
    return true;
}

} // namespace pocket::audio
