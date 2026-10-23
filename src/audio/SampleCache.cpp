#include "audio/SampleCache.h"

namespace pocket::audio {

SampleCache::SampleCache(float* storage,
                         std::size_t capacityFrames) noexcept {
    if (storage != nullptr && capacityFrames != 0 &&
        capacityFrames <= MaxCacheFrames) {
        storage_ = storage;
        capacityFrames_ = capacityFrames;
    }
}

SampleCacheLoadResult SampleCache::loadWave(
    std::size_t track,
    const std::uint8_t* fileData,
    std::size_t fileSize) noexcept {
    SampleCacheLoadResult loadResult;
    if (!isValid()) {
        loadResult.error = SampleCacheError::InvalidStorage;
        return loadResult;
    }
    if (track >= entries_.size()) {
        loadResult.error = SampleCacheError::TrackOutOfRange;
        return loadResult;
    }
    if (entries_[track].loaded) {
        loadResult.error = SampleCacheError::AlreadyLoaded;
        return loadResult;
    }
    if (fileData == nullptr) {
        loadResult.error = SampleCacheError::DecodeFailed;
        loadResult.decodeError = WavDecodeError::InvalidArgument;
        return loadResult;
    }

    std::size_t candidateOffset = 0;
    for (std::size_t attempt = 0; attempt <= entries_.size(); ++attempt) {
        std::size_t nextOccupiedOffset = capacityFrames_;
        for (const auto& entry : entries_) {
            if (entry.loaded && entry.offset >= candidateOffset &&
                entry.offset < nextOccupiedOffset) {
                nextOccupiedOffset = entry.offset;
            }
        }

        if (nextOccupiedOffset < candidateOffset) {
            loadResult.error = SampleCacheError::NoContiguousSpace;
            return loadResult;
        }
        const auto gapCapacity = nextOccupiedOffset - candidateOffset;
        if (gapCapacity != 0) {
            const auto decoded = WavDecoder::decodeMono(
                fileData, fileSize, storage_ + candidateOffset, gapCapacity);
            if (decoded) {
                entries_[track] = {
                    candidateOffset, decoded.frameCount, decoded.sampleRate,
                    true};
                usedFrames_ += decoded.frameCount;
                return {SampleCacheError::None, WavDecodeError::None,
                        decoded.frameCount, decoded.sampleRate};
            }
            if (decoded.error != WavDecodeError::OutputTooSmall) {
                loadResult.error = SampleCacheError::DecodeFailed;
                loadResult.decodeError = decoded.error;
                return loadResult;
            }
        }

        if (nextOccupiedOffset == capacityFrames_) {
            break;
        }
        std::size_t actualEnd = candidateOffset;
        for (const auto& entry : entries_) {
            if (entry.loaded && entry.offset == nextOccupiedOffset) {
                actualEnd = entry.offset + entry.frameCount;
                break;
            }
        }
        candidateOffset = actualEnd;
    }

    loadResult.error = SampleCacheError::NoContiguousSpace;
    return loadResult;
}

bool SampleCache::unload(std::size_t track) noexcept {
    if (track >= entries_.size() || !entries_[track].loaded) {
        return false;
    }
    usedFrames_ -= entries_[track].frameCount;
    entries_[track] = {};
    return true;
}

bool SampleCache::sample(std::size_t track,
                         MonoSampleView& result) const noexcept {
    if (!isValid() || track >= entries_.size() || !entries_[track].loaded) {
        return false;
    }
    const auto& entry = entries_[track];
    result = {storage_ + entry.offset, entry.frameCount, entry.sampleRate};
    return true;
}

} // namespace pocket::audio
