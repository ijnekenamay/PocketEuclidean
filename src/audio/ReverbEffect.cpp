#include "audio/ReverbEffect.h"

#include <algorithm>

namespace pocket::audio {
namespace {

constexpr std::array<std::size_t, ReverbEffect::CombCount> CombLengthsLeft{
    1116, 1188, 1277, 1356};
constexpr std::array<std::size_t, ReverbEffect::CombCount> CombLengthsRight{
    1139, 1211, 1300, 1379};
constexpr std::array<std::size_t, ReverbEffect::AllpassCount> AllpassLengthsLeft{
    556, 441};
constexpr std::array<std::size_t, ReverbEffect::AllpassCount> AllpassLengthsRight{
    579, 464};

constexpr float CombFeedback = 0.84F;
constexpr float CombDamping = 0.25F;
constexpr float AllpassFeedback = 0.5F;
constexpr float WetGain = 0.6F;

std::size_t scaledLength(std::size_t base, std::uint32_t sampleRate,
                         std::size_t maxFrames) noexcept {
    const std::size_t scaled =
        (base * static_cast<std::size_t>(sampleRate) + 22050U) / 44100U;
    return std::clamp<std::size_t>(scaled, 1, maxFrames - 1);
}

} // namespace

void ReverbEffect::reset() noexcept {
    for (auto* bank : {&combLeft_, &combRight_}) {
        for (auto& comb : *bank) {
            std::fill(comb.buffer.begin(), comb.buffer.end(), 0.0F);
            comb.index = 0;
            comb.dampingStore = 0.0F;
        }
    }
    for (auto* bank : {&allpassLeft_, &allpassRight_}) {
        for (auto& allpass : *bank) {
            std::fill(allpass.buffer.begin(), allpass.buffer.end(), 0.0F);
            allpass.index = 0;
        }
    }
    configuredRate_ = 0;
}

void ReverbEffect::configure(std::uint32_t sampleRate) noexcept {
    if (sampleRate < 8000 || sampleRate > 192000 ||
        sampleRate == configuredRate_) {
        return;
    }
    for (std::size_t i = 0; i < CombCount; ++i) {
        combLeft_[i].size =
            scaledLength(CombLengthsLeft[i], sampleRate, MaxCombFrames);
        combRight_[i].size =
            scaledLength(CombLengthsRight[i], sampleRate, MaxCombFrames);
    }
    for (std::size_t i = 0; i < AllpassCount; ++i) {
        allpassLeft_[i].size =
            scaledLength(AllpassLengthsLeft[i], sampleRate, MaxAllpassFrames);
        allpassRight_[i].size =
            scaledLength(AllpassLengthsRight[i], sampleRate, MaxAllpassFrames);
    }
    configuredRate_ = sampleRate;
}

float ReverbEffect::processComb(Comb& comb, float input, float feedback,
                                float damping) noexcept {
    const float delayed = comb.buffer[comb.index];
    comb.dampingStore = delayed * (1.0F - damping) + comb.dampingStore * damping;
    comb.buffer[comb.index] =
        std::clamp(input + comb.dampingStore * feedback, -4.0F, 4.0F);
    comb.index = (comb.index + 1) % comb.size;
    return delayed;
}

float ReverbEffect::processAllpass(Allpass& allpass, float input) noexcept {
    const float delayed = allpass.buffer[allpass.index];
    const float output = -input + delayed;
    allpass.buffer[allpass.index] = input + delayed * AllpassFeedback;
    allpass.index = (allpass.index + 1) % allpass.size;
    return output;
}

void ReverbEffect::processSendStereo(float* interleavedStereo,
                                     std::size_t frameCount,
                                     std::uint32_t sampleRate) noexcept {
    if (interleavedStereo == nullptr || frameCount == 0 || sampleRate < 8000 ||
        sampleRate > 192000) {
        return;
    }
    configure(sampleRate);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const float inputLeft = interleavedStereo[frame * 2];
        const float inputRight = interleavedStereo[frame * 2 + 1];

        float left = 0.0F;
        float right = 0.0F;
        for (std::size_t i = 0; i < CombCount; ++i) {
            left += processComb(combLeft_[i], inputLeft, CombFeedback,
                                CombDamping);
            right += processComb(combRight_[i], inputRight, CombFeedback,
                                 CombDamping);
        }
        left *= 0.25F;
        right *= 0.25F;
        for (std::size_t i = 0; i < AllpassCount; ++i) {
            left = processAllpass(allpassLeft_[i], left);
            right = processAllpass(allpassRight_[i], right);
        }

        interleavedStereo[frame * 2] = left * WetGain;
        interleavedStereo[frame * 2 + 1] = right * WetGain;
    }
}

} // namespace pocket::audio
