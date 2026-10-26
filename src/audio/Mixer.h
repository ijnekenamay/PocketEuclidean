#pragma once

#include "sequencer/Sequencer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pocket::audio {

struct StereoInputBuffer {
    const float* left = nullptr;
    const float* right = nullptr;
};

struct TrackMixerSettings {
    std::uint8_t volumePercent = 100;
    float pan = 0.0F;
    bool muted = false;
    float reverbSend = 0.0F;
    float delaySend = 0.0F;
};

struct StereoOutputBuffer {
    float* left = nullptr;
    float* right = nullptr;
};

// Optional post-fader effect sends. Each pointer addresses an interleaved
// stereo scratch buffer that receives the track sum for that send.
struct SendBuffers {
    float* reverb = nullptr;
    float* delay = nullptr;
};

class PeakMeters {
public:
    PeakMeters() noexcept;

    void publishTrack(std::size_t track, float peak) noexcept;
    void publishMaster(float peak) noexcept;
    float track(std::size_t track) const noexcept;
    float master() const noexcept;
    bool isLockFree() const noexcept;

private:
    std::array<std::atomic<float>, TrackCount> trackPeaks_{};
    std::atomic<float> masterPeak_{0.0F};
};

class Mixer {
public:
    static constexpr float GainSmoothingSeconds = 0.005F;
    static constexpr float TrackHeadroom =
        0.3535533905932738F;
    static constexpr float SoftLimitThreshold = 0.9F;

    Mixer() noexcept = default;

    static float volumePercentToGain(std::uint8_t volumePercent) noexcept;
    static float panLeftGain(float pan) noexcept;
    static float panRightGain(float pan) noexcept;
    static TrackMixerSettings settingsForTrack(
        const TrackState& track) noexcept;

    bool processBlock(const std::array<StereoInputBuffer, TrackCount>& inputs,
                      const std::array<TrackMixerSettings, TrackCount>& tracks,
                      std::uint8_t masterVolumePercent,
                      std::uint32_t sampleRate,
                      const StereoOutputBuffer& output,
                      std::size_t frameCount,
                      const SendBuffers* sends = nullptr) noexcept;

    const PeakMeters& meters() const noexcept { return meters_; }
    bool isRealtimeSafe() const noexcept { return meters_.isLockFree(); }

private:
    static float softLimit(float sample) noexcept;
    float smooth(float current, float target, float coefficient) noexcept;

    std::array<float, TrackCount> smoothedVolume_{};
    std::array<float, TrackCount> smoothedPan_{};
    std::array<float, TrackCount> smoothedReverbSend_{};
    std::array<float, TrackCount> smoothedDelaySend_{};
    float smoothedMasterVolume_ = 0.0F;
    PeakMeters meters_{};
};

} // namespace pocket::audio
