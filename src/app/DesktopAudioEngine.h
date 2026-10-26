#pragma once

#include "app/SdlAudioBackend.h"
#include "audio/AudioCallbackProcessor.h"
#include "audio/SelectableSynthVoice.h"
#include "audio/AudioParameterQueue.h"
#include "audio/Mixer.h"
#include "audio/SampleCache.h"
#include "audio/SequencerCommandQueue.h"
#include "audio/SamplerMixerRenderer.h"
#include "audio/SamplerVoicePool.h"
#include "sequencer/Sequencer.h"
#include "ui/PlayheadMirror.h"
#include "ui/StepLightBank.h"
#include "ui/StepTriggerEventQueue.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <vector>

namespace pocket::app {

// Desktop wiring of the platform-independent audio path. Built-in synthetic
// sounds stand in for WAV samples until the file loader is integrated.
class DesktopAudioEngine {
public:
    static constexpr std::uint32_t SourceSampleRate = 48000;

    explicit DesktopAudioEngine(const Sequencer& uiSequencer)
        : audioSequencer_(uiSequencer),
          sync_(sequencerQueue_, uiSequencer),
          renderer_(audioSequencer_, voices_, synthRack_, mixer_),
          processor_(audioSequencer_, events_, renderer_) {
        for (std::size_t track = 0; track < TrackCount; ++track) {
            samples_[track] = makeSound(track);
            voices_.assignSample(
                track, {samples_[track].data(), samples_[track].size(),
                        SourceSampleRate});
            const auto& synth = uiSequencer.track(track).synth;
            audio::SynthParameters params;
            params.pitchHz = synth.pitchHz;
            params.morph = synth.morph;
            params.timbre = synth.timbre;
            params.color = synth.color;
            params.level = 0.7F;
            params.engine = synth.engineType;
            synthRack_.configure(track, &synthVoices_[track], params);
        }
        processor_.setParameterQueue(&queue_);
        processor_.setSequencerCommandQueue(&sequencerQueue_);
        processor_.setPlayheadMirror(&playheadMirror_);
        processor_.setModulationMirror(&modulationMirror_);
    }

    ui::PlayheadMirror& playhead() noexcept { return playheadMirror_; }
    const ui::PlayheadMirror& playhead() const noexcept { return playheadMirror_; }
    ui::ModulationMirror& modulation() noexcept { return modulationMirror_; }
    const ui::ModulationMirror& modulation() const noexcept {
        return modulationMirror_;
    }

    // UI thread: forwards UI-side Sequencer edits to the audio thread's copy.
    void syncSequencer(const Sequencer& uiSequencer) {
        sync_.sync(uiSequencer);
    }

    // Loads *.wav files (sorted by name) into tracks 1..8 on the calling
    // thread. Must be called before start(); returns the number loaded.
    std::size_t loadSampleDirectory(const std::filesystem::path& dir,
                                    std::ostream& log) {
        if (backend_.isOpen()) {
            return 0;
        }
        std::error_code ec;
        std::vector<std::filesystem::path> files;
        for (const auto& entry :
             std::filesystem::directory_iterator(dir, ec)) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (entry.is_regular_file(ec) && ext == ".wav") {
                files.push_back(entry.path());
            }
        }
        if (ec) {
            log << "Cannot read sample directory: " << ec.message() << '\n';
            return 0;
        }
        std::sort(files.begin(), files.end());

        std::size_t loaded = 0;
        for (const auto& file : files) {
            if (loaded >= TrackCount) {
                break;
            }
            std::ifstream in(file, std::ios::binary);
            std::vector<std::uint8_t> bytes(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
            const auto result =
                cache_.loadWave(loaded, bytes.data(), bytes.size());
            if (!result) {
                log << "Skipped " << file.filename().string()
                    << " (decode error "
                    << static_cast<int>(result.decodeError) << ")\n";
                continue;
            }
            audio::MonoSampleView view;
            if (cache_.sample(loaded, view)) {
                voices_.assignSample(loaded, view);
                log << "Track " << (loaded + 1) << " <- "
                    << file.filename().string() << " (" << view.frameCount
                    << " frames @ " << view.sampleRate << " Hz)\n";
                ++loaded;
            }
        }
        return loaded;
    }

    bool start(const platform::AudioDeviceSpec& requested) {
        if (!processor_.isRealtimeSafe()) {
            return false;
        }
        platform::AudioDeviceSpec obtained;
        if (!backend_.open(requested, &audio::AudioCallbackProcessor::callback,
                           &processor_, obtained)) {
            return false;
        }
        obtained_ = obtained;
        return true;
    }

    void stop() { backend_.close(); }
    bool running() const noexcept { return backend_.isOpen(); }

    void setEffectState(const project::ProjectEffectState& state) noexcept {
        renderer_.setEffectState(state);
    }
    audio::AudioParameterQueue& parameterQueue() noexcept { return queue_; }
    const platform::AudioDeviceSpec& obtainedSpec() const noexcept {
        return obtained_;
    }
    std::uint32_t callbackFailures() const noexcept {
        return processor_.callbackFailureCount();
    }
    std::uint32_t droppedVisualEvents() const noexcept {
        return events_.droppedCount();
    }

    SdlAudioBackend::TimingReport timingReport() const noexcept {
        return backend_.timingReport();
    }

    // Call from the UI thread only.
    void updateLights() {
        lights_.consume(events_, backend_.framesRendered(),
                        backend_.sampleRate());
    }

    float luminance(std::size_t track, std::size_t step) const noexcept {
        float value = 0.0F;
        if (!lights_.valueAt(track, step, backend_.framesRendered(),
                             backend_.sampleRate(), value)) {
            return 0.0F;
        }
        return value;
    }

    SDL_AudioDeviceID device() const noexcept { return backend_.device(); }

    // Call from the UI thread each frame: instant attack, ~0.3 s release.
    float trackPeak(std::size_t track) const noexcept {
        return track < TrackCount ? shownTrackPeaks_[track] : 0.0F;
    }
    float masterPeak() const noexcept { return shownMasterPeak_; }
    void updateMeters() noexcept {
        constexpr float Release = 0.9F;
        for (std::size_t track = 0; track < TrackCount; ++track) {
            shownTrackPeaks_[track] = std::max(mixer_.meters().track(track),
                                               shownTrackPeaks_[track] * Release);
        }
        shownMasterPeak_ =
            std::max(mixer_.meters().master(), shownMasterPeak_ * Release);
    }

private:
    static std::vector<float> makeSound(std::size_t track) {
        constexpr double Pi = 3.14159265358979323846;
        const double seconds = track < 2 ? 0.3 : (track < 5 ? 0.12 : 0.2);
        const auto frames = static_cast<std::size_t>(seconds * SourceSampleRate);
        std::vector<float> data(frames);
        std::uint32_t noise = 0x1234567U + static_cast<std::uint32_t>(track);
        double phase = 0.0;
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / SourceSampleRate;
            const double decay = std::exp(-t / (seconds / 5.0));
            double value = 0.0;
            if (track == 0) {
                phase += 2.0 * Pi * (50.0 + 110.0 * std::exp(-t * 30.0)) /
                         SourceSampleRate;
                value = std::sin(phase);
            } else if (track == 1 || track == 2) {
                noise = noise * 1664525U + 1013904223U;
                const double n =
                    static_cast<double>(noise >> 8) / 8388608.0 - 1.0;
                phase += 2.0 * Pi * 180.0 / SourceSampleRate;
                value = track == 1 ? 0.6 * n + 0.4 * std::sin(phase) : n;
            } else {
                phase += 2.0 * Pi * (220.0 * std::pow(1.2599, track - 3.0)) /
                         SourceSampleRate;
                value = std::sin(phase);
            }
            data[i] = static_cast<float>(0.8 * value * decay);
        }
        return data;
    }

    ui::StepTriggerEventQueue events_;
    audio::AudioParameterQueue queue_;
    audio::SequencerCommandQueue sequencerQueue_;
    Sequencer audioSequencer_;
    audio::SequencerSync sync_;
    audio::SamplerVoicePool voices_;
    std::array<audio::SelectableSynthVoice, TrackCount> synthVoices_{};
    audio::SynthVoiceRack synthRack_{};
    audio::Mixer mixer_;
    audio::SamplerMixerRenderer renderer_;
    audio::AudioCallbackProcessor processor_;
    ui::PlayheadMirror playheadMirror_;
    ui::ModulationMirror modulationMirror_;
    ui::StepLightBank lights_;
    std::vector<float> cacheStorage_ = std::vector<float>(8U * 1024U * 1024U);
    audio::SampleCache cache_{cacheStorage_.data(), cacheStorage_.size()};
    SdlAudioBackend backend_;
    platform::AudioDeviceSpec obtained_{};
    std::array<std::vector<float>, TrackCount> samples_{};
    std::array<float, TrackCount> shownTrackPeaks_{};
    float shownMasterPeak_ = 0.0F;
};

} // namespace pocket::app
