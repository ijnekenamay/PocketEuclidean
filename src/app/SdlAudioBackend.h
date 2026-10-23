#pragma once

#include "platform/BackendInterfaces.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace pocket::app {

// SDL2 implementation of IAudioBackend. SDL may hand over more frames than the
// processor accepts per call, so each SDL buffer is split into contiguous
// chunks while blockStartFrame stays monotonic from zero per open().
class SdlAudioBackend : public platform::IAudioBackend {
public:
    static constexpr std::uint32_t MaxChunkFrames = 4096;

    ~SdlAudioBackend() override { close(); }

    bool open(const platform::AudioDeviceSpec& requested,
              platform::AudioCallback callback,
              void* userData,
              platform::AudioDeviceSpec& obtained) noexcept override {
        if (device_ != 0 || callback == nullptr || !platform::isValid(requested)) {
            return false;
        }
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            return false;
        }

        SDL_AudioSpec want{};
        want.freq = static_cast<int>(requested.sampleRate);
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = requested.bufferFrames;
        want.callback = &SdlAudioBackend::sdlCallback;
        want.userdata = this;

        SDL_AudioSpec have{};
        callback_ = callback;
        userData_ = userData;
        blockStartFrame_ = 0;
        framesRendered_.store(0, std::memory_order_relaxed);

        device_ = SDL_OpenAudioDevice(
            nullptr, 0, &want, &have,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
        if (device_ == 0) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        if (have.format != AUDIO_F32SYS || have.channels != 2) {
            SDL_CloseAudioDevice(device_);
            device_ = 0;
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }

        sampleRate_ = static_cast<std::uint32_t>(have.freq);
        obtained.sampleRate = sampleRate_;
        obtained.bufferFrames = have.samples;
        if (!platform::isValid(obtained)) {
            close();
            return false;
        }
        SDL_PauseAudioDevice(device_, 0);
        return true;
    }

    void close() noexcept override {
        if (device_ == 0) {
            return;
        }
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    bool isOpen() const noexcept { return device_ != 0; }
    SDL_AudioDeviceID device() const noexcept { return device_; }
    std::uint32_t sampleRate() const noexcept { return sampleRate_; }

    // Frames whose rendering has completed; the UI uses it as the audio clock.
    std::uint64_t framesRendered() const noexcept {
        return framesRendered_.load(std::memory_order_acquire);
    }

    // Callback load = processing time / buffer duration, bucketed in 1% steps
    // (bucket 100 holds everything >= 100%, i.e. a missed deadline).
    struct TimingReport {
        std::uint64_t callbacks = 0;
        std::uint64_t overBudget = 0;
        double maxLoadPercent = 0.0;
        double meanLoadPercent = 0.0;
        double p99LoadPercent = 0.0;
    };

    TimingReport timingReport() const noexcept {
        TimingReport report;
        report.callbacks = timingCount_.load(std::memory_order_relaxed);
        if (report.callbacks == 0) {
            return report;
        }
        report.overBudget = overBudget_.load(std::memory_order_relaxed);
        report.maxLoadPercent =
            static_cast<double>(maxLoadPermille_.load(std::memory_order_relaxed)) /
            10.0;
        report.meanLoadPercent =
            static_cast<double>(totalLoadPermille_.load(std::memory_order_relaxed)) /
            static_cast<double>(report.callbacks) / 10.0;
        const auto target = (report.callbacks * 99 + 99) / 100;
        std::uint64_t seen = 0;
        for (std::size_t bucket = 0; bucket < loadBuckets_.size(); ++bucket) {
            seen += loadBuckets_[bucket].load(std::memory_order_relaxed);
            if (seen >= target) {
                report.p99LoadPercent = static_cast<double>(bucket + 1);
                break;
            }
        }
        return report;
    }

private:
    void recordTiming(std::uint64_t elapsedNs, std::uint32_t frames) noexcept {
        const double budgetNs =
            1.0e9 * static_cast<double>(frames) / static_cast<double>(sampleRate_);
        const double load = budgetNs > 0.0 ? elapsedNs / budgetNs : 0.0;
        const auto permille = static_cast<std::uint64_t>(load * 1000.0);
        const auto bucket = std::min<std::size_t>(
            static_cast<std::size_t>(load * 100.0), loadBuckets_.size() - 1);
        loadBuckets_[bucket].fetch_add(1, std::memory_order_relaxed);
        timingCount_.fetch_add(1, std::memory_order_relaxed);
        totalLoadPermille_.fetch_add(permille, std::memory_order_relaxed);
        if (load >= 1.0) {
            overBudget_.fetch_add(1, std::memory_order_relaxed);
        }
        auto previous = maxLoadPermille_.load(std::memory_order_relaxed);
        while (permille > previous &&
               !maxLoadPermille_.compare_exchange_weak(
                   previous, permille, std::memory_order_relaxed)) {
        }
    }

    static void SDLCALL sdlCallback(void* userData,
                                    Uint8* stream,
                                    int length) {
        auto& self = *static_cast<SdlAudioBackend*>(userData);
        auto* samples = reinterpret_cast<float*>(stream);
        std::uint32_t remaining = static_cast<std::uint32_t>(
            static_cast<std::size_t>(length) / (2 * sizeof(float)));
        while (remaining > 0) {
            const auto chunk = std::min(remaining, MaxChunkFrames);
            const auto begin = std::chrono::steady_clock::now();
            self.callback_(self.userData_, samples, chunk, self.blockStartFrame_,
                           self.sampleRate_);
            const auto elapsedNs = std::chrono::duration_cast<
                std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                          begin).count();
            self.recordTiming(static_cast<std::uint64_t>(elapsedNs), chunk);
            self.blockStartFrame_ += chunk;
            self.framesRendered_.store(self.blockStartFrame_,
                                       std::memory_order_release);
            samples += static_cast<std::size_t>(chunk) * 2;
            remaining -= chunk;
        }
    }

    SDL_AudioDeviceID device_ = 0;
    platform::AudioCallback callback_ = nullptr;
    void* userData_ = nullptr;
    std::uint32_t sampleRate_ = 48000;
    std::uint64_t blockStartFrame_ = 0;
    std::atomic<std::uint64_t> framesRendered_{0};
    std::array<std::atomic<std::uint64_t>, 101> loadBuckets_{};
    std::atomic<std::uint64_t> timingCount_{0};
    std::atomic<std::uint64_t> overBudget_{0};
    std::atomic<std::uint64_t> totalLoadPermille_{0};
    std::atomic<std::uint64_t> maxLoadPermille_{0};
};

} // namespace pocket::app
