#include "audio/WavDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace pocket::audio {
namespace {

constexpr std::uint16_t PcmFormat = 1;
constexpr std::uint16_t FloatFormat = 3;
constexpr std::uint16_t ExtensibleFormat = 0xfffe;
constexpr std::uint16_t PcmSubtype = 1;
constexpr std::uint16_t FloatSubtype = 3;

std::uint16_t read16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8U);
}

std::uint32_t read32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool hasId(const std::uint8_t* bytes, const char* id) noexcept {
    return bytes[0] == static_cast<std::uint8_t>(id[0]) &&
           bytes[1] == static_cast<std::uint8_t>(id[1]) &&
           bytes[2] == static_cast<std::uint8_t>(id[2]) &&
           bytes[3] == static_cast<std::uint8_t>(id[3]);
}

float decodeInteger(const std::uint8_t* bytes,
                    std::uint16_t bitsPerSample) noexcept {
    switch (bitsPerSample) {
    case 8:
        return (static_cast<float>(bytes[0]) - 128.0F) / 128.0F;
    case 16: {
        const auto raw = read16(bytes);
        const auto signedValue = raw < 0x8000U
                                     ? static_cast<std::int32_t>(raw)
                                     : static_cast<std::int32_t>(raw) - 0x10000;
        return static_cast<float>(signedValue) / 32768.0F;
    }
    case 24: {
        std::int32_t signedValue =
            static_cast<std::int32_t>(bytes[0]) |
            (static_cast<std::int32_t>(bytes[1]) << 8U) |
            (static_cast<std::int32_t>(bytes[2]) << 16U);
        if ((signedValue & 0x00800000) != 0) {
            signedValue -= 0x01000000;
        }
        return static_cast<float>(signedValue) / 8388608.0F;
    }
    case 32: {
        const auto raw = read32(bytes);
        std::int32_t signedValue = 0;
        std::memcpy(&signedValue, &raw, sizeof(signedValue));
        return static_cast<float>(
                   static_cast<double>(signedValue) / 2147483648.0);
    }
    default:
        return 0.0F;
    }
}

float decodeFloat32(const std::uint8_t* bytes) noexcept {
    const auto bits = read32(bytes);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool isSupportedIntegerDepth(std::uint16_t bitsPerSample) noexcept {
    return bitsPerSample == 8 || bitsPerSample == 16 ||
           bitsPerSample == 24 || bitsPerSample == 32;
}

} // namespace

WavDecodeResult WavDecoder::decodeMono(
    const std::uint8_t* fileData,
    std::size_t fileSize,
    float* output,
    std::size_t outputCapacityFrames) noexcept {
    WavDecodeResult result;
    if (fileData == nullptr || output == nullptr || fileSize < 12) {
        result.error = WavDecodeError::InvalidArgument;
        return result;
    }
    if (!hasId(fileData, "RIFF") || !hasId(fileData + 8, "WAVE")) {
        result.error = WavDecodeError::InvalidContainer;
        return result;
    }

    const std::uint32_t riffSize32 = read32(fileData + 4);
    const std::size_t riffSize = static_cast<std::size_t>(riffSize32);
    if (riffSize < 4 || riffSize > fileSize - 8) {
        result.error = WavDecodeError::InvalidContainer;
        return result;
    }
    const std::size_t riffEnd = 8 + riffSize;

    bool hasFormat = false;
    bool hasData = false;
    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint16_t blockAlign = 0;
    std::uint32_t sampleRate = 0;
    const std::uint8_t* sampleData = nullptr;
    std::size_t sampleDataSize = 0;

    std::size_t offset = 12;
    while (offset < riffEnd) {
        if (riffEnd - offset < 8) {
            result.error = WavDecodeError::InvalidContainer;
            return result;
        }
        const auto chunkSize32 = read32(fileData + offset + 4);
        const auto chunkSize = static_cast<std::size_t>(chunkSize32);
        const std::size_t chunkDataOffset = offset + 8;
        if (chunkSize > riffEnd - chunkDataOffset) {
            result.error = WavDecodeError::InvalidContainer;
            return result;
        }

        if (hasId(fileData + offset, "fmt ")) {
            if (hasFormat || chunkSize < 16) {
                result.error = WavDecodeError::InvalidFormat;
                return result;
            }
            const auto* chunk = fileData + chunkDataOffset;
            format = read16(chunk);
            channels = read16(chunk + 2);
            sampleRate = read32(chunk + 4);
            const auto byteRate = read32(chunk + 8);
            blockAlign = read16(chunk + 12);
            bitsPerSample = read16(chunk + 14);

            if (format == ExtensibleFormat) {
                if (chunkSize < 40 || read16(chunk + 16) < 22 ||
                    static_cast<std::size_t>(read16(chunk + 16)) >
                        chunkSize - 18 ||
                    read16(chunk + 18) != bitsPerSample ||
                    chunk[28] != 0x00 || chunk[29] != 0x00 ||
                    chunk[30] != 0x10 || chunk[31] != 0x00 ||
                    chunk[32] != 0x80 || chunk[33] != 0x00 ||
                    chunk[34] != 0x00 || chunk[35] != 0xaa ||
                    chunk[36] != 0x00 || chunk[37] != 0x38 ||
                    chunk[38] != 0x9b || chunk[39] != 0x71) {
                    result.error = WavDecodeError::UnsupportedFormat;
                    return result;
                }
                const auto subtype = read32(chunk + 24);
                if (subtype != PcmSubtype && subtype != FloatSubtype) {
                    result.error = WavDecodeError::UnsupportedFormat;
                    return result;
                }
                format = static_cast<std::uint16_t>(subtype);
            }
            hasFormat = true;

            if ((format != PcmFormat && format != FloatFormat) ||
                (format == PcmFormat &&
                 !isSupportedIntegerDepth(bitsPerSample)) ||
                (format == FloatFormat && bitsPerSample != 32)) {
                result.error = WavDecodeError::UnsupportedFormat;
                return result;
            }
            if ((channels != 1 && channels != 2) || sampleRate < 8000 ||
                sampleRate > 192000) {
                result.error = WavDecodeError::InvalidFormat;
                return result;
            }
            const std::uint32_t bytesPerSample = bitsPerSample / 8;
            const std::uint32_t expectedAlign =
                static_cast<std::uint32_t>(channels) * bytesPerSample;
            if (blockAlign != expectedAlign ||
                sampleRate > std::numeric_limits<std::uint32_t>::max() /
                                 expectedAlign ||
                byteRate != sampleRate * expectedAlign) {
                result.error = WavDecodeError::InvalidFormat;
                return result;
            }
        } else if (hasId(fileData + offset, "data")) {
            if (hasData) {
                result.error = WavDecodeError::InvalidData;
                return result;
            }
            sampleData = fileData + chunkDataOffset;
            sampleDataSize = chunkSize;
            hasData = true;
        }

        if ((chunkSize & 1U) != 0 &&
            chunkSize == riffEnd - chunkDataOffset) {
            result.error = WavDecodeError::InvalidContainer;
            return result;
        }
        const std::size_t paddedSize = chunkSize + (chunkSize & 1U);
        if (paddedSize > riffEnd - chunkDataOffset) {
            result.error = WavDecodeError::InvalidContainer;
            return result;
        }
        offset = chunkDataOffset + paddedSize;
    }

    if (!hasFormat) {
        result.error = WavDecodeError::MissingFormat;
        return result;
    }
    if (!hasData) {
        result.error = WavDecodeError::MissingData;
        return result;
    }
    if (sampleDataSize == 0 || sampleDataSize % blockAlign != 0) {
        result.error = WavDecodeError::InvalidData;
        return result;
    }

    const auto frameCount = sampleDataSize / blockAlign;
    if (frameCount > MaxDecodedFrames) {
        result.error = WavDecodeError::DecodedSampleTooLarge;
        return result;
    }
    if (frameCount > outputCapacityFrames) {
        result.error = WavDecodeError::OutputTooSmall;
        return result;
    }

    const auto bytesPerSample = bitsPerSample / 8;
    if (format == FloatFormat) {
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            for (std::uint16_t channel = 0; channel < channels; ++channel) {
                const auto* sample =
                    sampleData + frame * blockAlign + channel * bytesPerSample;
                if (!std::isfinite(decodeFloat32(sample))) {
                    result.error = WavDecodeError::NonFiniteSample;
                    return result;
                }
            }
        }
    }

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        float left = format == PcmFormat
                         ? decodeInteger(sampleData + frame * blockAlign,
                                         bitsPerSample)
                         : decodeFloat32(sampleData + frame * blockAlign);
        if (!std::isfinite(left)) {
            result.error = WavDecodeError::NonFiniteSample;
            return result;
        }

        if (channels == 2) {
            const auto* rightData =
                sampleData + frame * blockAlign + bytesPerSample;
            const float right =
                format == PcmFormat
                    ? decodeInteger(rightData, bitsPerSample)
                    : decodeFloat32(rightData);
            if (!std::isfinite(right)) {
                result.error = WavDecodeError::NonFiniteSample;
                return result;
            }
            left = 0.5F * (left + right);
        }
        output[frame] = std::clamp(left, -1.0F, 1.0F);
    }

    result.error = WavDecodeError::None;
    result.frameCount = frameCount;
    result.sampleRate = sampleRate;
    return result;
}

} // namespace pocket::audio
