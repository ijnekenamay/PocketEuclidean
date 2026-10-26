#include "audio/Mixer.h"

#include <algorithm>
#include <cmath>

namespace pocket::audio {

PeakMeters::PeakMeters() noexcept {
    for (auto& peak : trackPeaks_) {
        peak.store(0.0F, std::memory_order_relaxed);
    }
    masterPeak_.store(0.0F, std::memory_order_relaxed);
}

void PeakMeters::publishTrack(std::size_t trackIndex, float peak) noexcept {
    if (trackIndex < trackPeaks_.size()) {
        trackPeaks_[trackIndex].store(peak, std::memory_order_release);
    }
}

void PeakMeters::publishMaster(float peak) noexcept {
    masterPeak_.store(peak, std::memory_order_release);
}

float PeakMeters::track(std::size_t trackIndex) const noexcept {
    return trackIndex < trackPeaks_.size()
               ? trackPeaks_[trackIndex].load(std::memory_order_acquire)
               : 0.0F;
}

float PeakMeters::master() const noexcept {
    return masterPeak_.load(std::memory_order_acquire);
}

bool PeakMeters::isLockFree() const noexcept {
    if (!masterPeak_.is_lock_free()) {
        return false;
    }
    for (const auto& peak : trackPeaks_) {
        if (!peak.is_lock_free()) {
            return false;
        }
    }
    return true;
}

float Mixer::volumePercentToGain(std::uint8_t volumePercent) noexcept {
    const float normalized =
        static_cast<float>(volumePercent > 100 ? 100 : volumePercent) / 100.0F;
    return normalized * normalized;
}

float Mixer::panLeftGain(float pan) noexcept {
    const float bounded = std::clamp(pan, -1.0F, 1.0F);
    return std::sqrt((1.0F - bounded) * 0.5F);
}

float Mixer::panRightGain(float pan) noexcept {
    const float bounded = std::clamp(pan, -1.0F, 1.0F);
    return std::sqrt((1.0F + bounded) * 0.5F);
}

TrackMixerSettings Mixer::settingsForTrack(const TrackState& track) noexcept {
    return {track.volume, track.pan, track.muted, track.reverbSend,
            track.delaySend};
}

bool Mixer::processBlock(
    const std::array<StereoInputBuffer, TrackCount>& inputs,
    const std::array<TrackMixerSettings, TrackCount>& tracks,
    std::uint8_t masterVolumePercent,
    std::uint32_t sampleRate,
    const StereoOutputBuffer& output,
    std::size_t frameCount,
    const SendBuffers* sends) noexcept {
    if (output.left == nullptr || output.right == nullptr || sampleRate == 0 ||
        masterVolumePercent > 100) {
        return false;
    }
    for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
        const auto& settings = tracks[trackIndex];
        if (inputs[trackIndex].left == nullptr ||
            settings.volumePercent > 100 || !std::isfinite(settings.pan) ||
            settings.pan < -1.0F || settings.pan > 1.0F ||
            !std::isfinite(settings.reverbSend) ||
            settings.reverbSend < 0.0F || settings.reverbSend > 1.0F ||
            !std::isfinite(settings.delaySend) ||
            settings.delaySend < 0.0F || settings.delaySend > 1.0F) {
            return false;
        }
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            if (!std::isfinite(inputs[trackIndex].left[frame]) ||
                (inputs[trackIndex].right != nullptr &&
                 !std::isfinite(inputs[trackIndex].right[frame]))) {
                return false;
            }
        }
    }

    if (sends != nullptr) {
        if (sends->reverb != nullptr) {
            std::fill_n(sends->reverb, frameCount * 2, 0.0F);
        }
        if (sends->delay != nullptr) {
            std::fill_n(sends->delay, frameCount * 2, 0.0F);
        }
    }

    const float smoothingCoefficient = static_cast<float>(
        std::exp(-1.0 / (GainSmoothingSeconds * sampleRate)));
    std::array<float, TrackCount> trackPeakValues{};
    float masterPeakValue = 0.0F;

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        float mixedLeft = 0.0F;
        float mixedRight = 0.0F;
        float reverbSendLeft = 0.0F;
        float reverbSendRight = 0.0F;
        float delaySendLeft = 0.0F;
        float delaySendRight = 0.0F;
        for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
            const auto& settings = tracks[trackIndex];
            const auto& input = inputs[trackIndex];
            const float targetVolume =
                settings.muted ? 0.0F : volumePercentToGain(settings.volumePercent);
            smoothedVolume_[trackIndex] = smooth(
                smoothedVolume_[trackIndex], targetVolume, smoothingCoefficient);
            smoothedPan_[trackIndex] = smooth(
                smoothedPan_[trackIndex], settings.pan, smoothingCoefficient);
            smoothedReverbSend_[trackIndex] = smooth(
                smoothedReverbSend_[trackIndex], settings.reverbSend,
                smoothingCoefficient);
            smoothedDelaySend_[trackIndex] = smooth(
                smoothedDelaySend_[trackIndex], settings.delaySend,
                smoothingCoefficient);

            const float leftPan = panLeftGain(smoothedPan_[trackIndex]);
            const float rightPan = panRightGain(smoothedPan_[trackIndex]);
            const float sourceLeft = input.left[frame];
            const bool mono = input.right == nullptr;
            const float sourceRight = mono ? sourceLeft : input.right[frame];
            float trackLeft = 0.0F;
            float trackRight = 0.0F;

            if (mono) {
                const float gain = smoothedVolume_[trackIndex] * TrackHeadroom;
                trackLeft = sourceLeft * gain * leftPan;
                trackRight = sourceRight * gain * rightPan;
            } else {
                const float gain = smoothedVolume_[trackIndex] * TrackHeadroom;
                const float balanceLeft =
                    smoothedPan_[trackIndex] > 0.0F
                        ? 1.0F - smoothedPan_[trackIndex]
                        : 1.0F;
                const float balanceRight =
                    smoothedPan_[trackIndex] < 0.0F
                        ? 1.0F + smoothedPan_[trackIndex]
                        : 1.0F;
                trackLeft = sourceLeft * gain * balanceLeft;
                trackRight = sourceRight * gain * balanceRight;
            }

            reverbSendLeft += trackLeft * smoothedReverbSend_[trackIndex];
            reverbSendRight += trackRight * smoothedReverbSend_[trackIndex];
            delaySendLeft += trackLeft * smoothedDelaySend_[trackIndex];
            delaySendRight += trackRight * smoothedDelaySend_[trackIndex];

            trackPeakValues[trackIndex] =
                std::max(trackPeakValues[trackIndex],
                         std::max(std::abs(trackLeft), std::abs(trackRight)));
            mixedLeft += trackLeft;
            mixedRight += trackRight;
        }

        const float targetMasterGain =
            volumePercentToGain(masterVolumePercent);
        smoothedMasterVolume_ = smooth(
            smoothedMasterVolume_, targetMasterGain, smoothingCoefficient);
        const float outputLeft =
            softLimit(mixedLeft * smoothedMasterVolume_);
        const float outputRight =
            softLimit(mixedRight * smoothedMasterVolume_);
        output.left[frame] = outputLeft;
        output.right[frame] = outputRight;
        masterPeakValue =
            std::max(masterPeakValue,
                     std::max(std::abs(outputLeft), std::abs(outputRight)));

        if (sends != nullptr) {
            // Sends are post-fader and follow the master level so the wet
            // signal stays balanced with the dry mix.
            if (sends->reverb != nullptr) {
                sends->reverb[frame * 2] = reverbSendLeft * smoothedMasterVolume_;
                sends->reverb[frame * 2 + 1] =
                    reverbSendRight * smoothedMasterVolume_;
            }
            if (sends->delay != nullptr) {
                sends->delay[frame * 2] = delaySendLeft * smoothedMasterVolume_;
                sends->delay[frame * 2 + 1] =
                    delaySendRight * smoothedMasterVolume_;
            }
        }
    }

    for (std::size_t trackIndex = 0; trackIndex < trackPeakValues.size();
         ++trackIndex) {
        meters_.publishTrack(trackIndex, trackPeakValues[trackIndex]);
    }
    meters_.publishMaster(masterPeakValue);
    return true;
}

float Mixer::softLimit(float sample) noexcept {
    const float magnitude = std::abs(sample);
    if (magnitude <= SoftLimitThreshold) {
        return sample;
    }
    const float normalized =
        (magnitude - SoftLimitThreshold) / (1.0F - SoftLimitThreshold);
    const float limited =
        SoftLimitThreshold +
        (1.0F - SoftLimitThreshold) * std::tanh(normalized);
    return std::copysign(limited, sample);
}

float Mixer::smooth(float current, float target, float coefficient) noexcept {
    return target + coefficient * (current - target);
}

} // namespace pocket::audio
