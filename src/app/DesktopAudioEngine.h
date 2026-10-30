#pragma once

#include "app/SdlAudioBackend.h"
#include "audio/AudioCallbackProcessor.h"
#include "audio/SelectableSynthVoice.h"
#include "audio/AudioParameterQueue.h"
#include "audio/Mixer.h"
#include "audio/SequencerCommandQueue.h"
#include "audio/SamplerMixerRenderer.h"
#include "audio/SamplerVoicePool.h"
#include "audio/WavDecoder.h"
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
#include <string>
#include <utility>
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
            params.model = synth.model;
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
            std::string error;
            if (loadSampleFile(loaded, file, &error)) {
                log << "Track " << (loaded + 1) << " <- "
                    << file.filename().string() << " ("
                    << sampleFrameCounts_[loaded] << " frames @ "
                    << sampleRates_[loaded] << " Hz)\n";
                ++loaded;
            } else {
                log << "Skipped " << file.filename().string() << " ("
                    << error << ")\n";
            }
        }
        return loaded;
    }

    // UI thread: decodes a WAV and queues it for the selected track. The
    // previous buffer stays alive until the audio thread acknowledges the
    // swap, so samples can be replaced while playback is running.
    bool loadSampleFile(std::size_t track,
                        const std::filesystem::path& path,
                        std::string* error = nullptr) {
        if (track >= TrackCount) {
            setError(error, "track out of range");
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            setError(error, "cannot open " + path.string());
            return false;
        }
        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        if (bytes.empty()) {
            setError(error, "empty WAV file");
            return false;
        }

        std::size_t capacity = std::clamp<std::size_t>(
            bytes.size() / 2 + 64, 4096,
            audio::WavDecoder::MaxDecodedFrames);
        std::vector<float> frames(capacity);
        auto decoded = audio::WavDecoder::decodeMono(
            bytes.data(), bytes.size(), frames.data(), frames.size());
        while (!decoded &&
               decoded.error == audio::WavDecodeError::OutputTooSmall &&
               capacity < audio::WavDecoder::MaxDecodedFrames) {
            capacity =
                std::min(capacity * 2, audio::WavDecoder::MaxDecodedFrames);
            frames.resize(capacity);
            decoded = audio::WavDecoder::decodeMono(
                bytes.data(), bytes.size(), frames.data(), frames.size());
        }
        if (!decoded) {
            setError(error, "decode error " +
                                std::to_string(
                                    static_cast<int>(decoded.error)));
            return false;
        }
        frames.resize(decoded.frameCount);
        const audio::MonoSampleView view{
            frames.data(), frames.size(), decoded.sampleRate};
        if (beginSwap(track, view, path.string(), error) == 0) {
            return false;
        }
        ownedSamples_[track] = std::move(frames);
        sampleFrameCounts_[track] = ownedSamples_[track].size();
        sampleRates_[track] = decoded.sampleRate;
        return true;
    }

    // UI thread: restores the built-in synthetic sample for a track.
    bool useBuiltinSample(std::size_t track, std::string* error = nullptr) {
        if (track >= TrackCount) {
            setError(error, "track out of range");
            return false;
        }
        const auto& builtin = samples_[track];
        const audio::MonoSampleView view{
            builtin.data(), builtin.size(), SourceSampleRate};
        if (beginSwap(track, view, {}, error) == 0) {
            return false;
        }
        ownedSamples_[track].clear();
        sampleFrameCounts_[track] = builtin.size();
        sampleRates_[track] = SourceSampleRate;
        return true;
    }

    // UI thread, once per frame: releases replaced buffers once the audio
    // thread reports that the swap and its voice stop have been applied.
    void updateSampleRetirement() {
        if (!backend_.isOpen()) {
            // No audio callback owns the queue yet, so apply on this thread.
            voices_.applyPendingSamples();
        }
        retiredSamples_.erase(
            std::remove_if(
                retiredSamples_.begin(), retiredSamples_.end(),
                [this](const RetiredSample& retired) {
                    return voices_.appliedSampleId(retired.track) >= retired.id;
                }),
            retiredSamples_.end());
    }

    const std::string& samplePath(std::size_t track) const noexcept {
        static const std::string empty;
        return track < TrackCount ? samplePaths_[track] : empty;
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
    // Setup-only. The processor owns no MIDI object; the app keeps the backend
    // alive and gates sends with an atomic flag.
    void setMidiBackend(platform::IMidiBackend* backend) noexcept {
        processor_.setMidiBackend(backend);
    }
    void setMidiOutputEnabled(bool enabled) noexcept {
        processor_.setMidiOutputEnabled(enabled);
    }
    void setMidiBaseChannel(std::uint8_t channel) noexcept {
        processor_.setMidiBaseChannel(channel);
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
    struct RetiredSample {
        std::size_t track = 0;
        std::uint32_t id = 0;
        std::vector<float> frames;
    };

    static void setError(std::string* error, const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
    }

    // Caller must keep view.samples alive until the swap is acknowledged.
    std::uint32_t beginSwap(std::size_t track,
                            const audio::MonoSampleView& view,
                            std::string path,
                            std::string* error) {
        const auto id = nextSampleId_ + 1;
        if (!voices_.queueSample(track, view, id)) {
            setError(error, "sample queue full");
            return 0;
        }
        nextSampleId_ = id;
        if (!ownedSamples_[track].empty()) {
            retiredSamples_.push_back(
                {track, id, std::move(ownedSamples_[track])});
        }
        samplePaths_[track] = std::move(path);
        return id;
    }

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
    SdlAudioBackend backend_;
    platform::AudioDeviceSpec obtained_{};
    std::array<std::vector<float>, TrackCount> samples_{};
    std::array<std::vector<float>, TrackCount> ownedSamples_{};
    std::array<std::string, TrackCount> samplePaths_{};
    std::array<std::size_t, TrackCount> sampleFrameCounts_{};
    std::array<std::uint32_t, TrackCount> sampleRates_{};
    std::vector<RetiredSample> retiredSamples_;
    std::uint32_t nextSampleId_ = 0;
    std::array<float, TrackCount> shownTrackPeaks_{};
    float shownMasterPeak_ = 0.0F;
};

} // namespace pocket::app
