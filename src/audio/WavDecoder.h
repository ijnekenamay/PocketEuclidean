#pragma once

#include "audio/SamplerVoicePool.h"

#include <cstddef>
#include <cstdint>

namespace pocket::audio {

enum class WavDecodeError : std::uint8_t {
    None,
    InvalidArgument,
    InvalidContainer,
    MissingFormat,
    MissingData,
    UnsupportedFormat,
    InvalidFormat,
    InvalidData,
    OutputTooSmall,
    DecodedSampleTooLarge,
    NonFiniteSample
};

struct WavDecodeResult {
    WavDecodeError error = WavDecodeError::InvalidArgument;
    std::size_t frameCount = 0;
    std::uint32_t sampleRate = 0;

    explicit operator bool() const noexcept {
        return error == WavDecodeError::None;
    }

    MonoSampleView sampleView(const float* samples) const noexcept {
        return *this ? MonoSampleView{samples, frameCount, sampleRate}
                     : MonoSampleView{};
    }
};

class WavDecoder {
public:
    static constexpr std::size_t MaxDecodedBytes = 64U * 1024U * 1024U;
    static constexpr std::size_t MaxDecodedFrames =
        MaxDecodedBytes / sizeof(float);

    static WavDecodeResult decodeMono(const std::uint8_t* fileData,
                                      std::size_t fileSize,
                                      float* output,
                                      std::size_t outputCapacityFrames) noexcept;
};

} // namespace pocket::audio
