#pragma once

#include "audio/WavDecoder.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket::audio {

enum class SampleCacheError : std::uint8_t {
    None,
    InvalidStorage,
    TrackOutOfRange,
    AlreadyLoaded,
    NoContiguousSpace,
    DecodeFailed
};

struct SampleCacheLoadResult {
    SampleCacheError error = SampleCacheError::InvalidStorage;
    WavDecodeError decodeError = WavDecodeError::None;
    std::size_t frameCount = 0;
    std::uint32_t sampleRate = 0;

    explicit operator bool() const noexcept {
        return error == SampleCacheError::None;
    }
};

class SampleCache {
public:
    static constexpr std::size_t MaxCacheBytes = 64U * 1024U * 1024U;
    static constexpr std::size_t MaxCacheFrames =
        MaxCacheBytes / sizeof(float);

    SampleCache(float* storage, std::size_t capacityFrames) noexcept;

    SampleCacheLoadResult loadWave(std::size_t track,
                                   const std::uint8_t* fileData,
                                   std::size_t fileSize) noexcept;
    bool unload(std::size_t track) noexcept;
    bool sample(std::size_t track, MonoSampleView& result) const noexcept;

    bool isValid() const noexcept { return storage_ != nullptr; }
    std::size_t usedFrames() const noexcept { return usedFrames_; }
    std::size_t usedBytes() const noexcept {
        return usedFrames_ * sizeof(float);
    }
    std::size_t capacityFrames() const noexcept { return capacityFrames_; }

private:
    struct Entry {
        std::size_t offset = 0;
        std::size_t frameCount = 0;
        std::uint32_t sampleRate = 0;
        bool loaded = false;
    };

    float* storage_ = nullptr;
    std::size_t capacityFrames_ = 0;
    std::size_t usedFrames_ = 0;
    std::array<Entry, TrackCount> entries_{};
};

} // namespace pocket::audio
