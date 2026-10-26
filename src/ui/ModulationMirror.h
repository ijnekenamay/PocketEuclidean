#pragma once

#include "sequencer/Modulation.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pocket::ui {

// Latest modulation state published by the audio thread. The UI only needs
// the newest value per track, so plain atomics are enough (no queue).
struct LiveModulationState {
    EffectiveRhythm rhythm{};
    float lfoPhase = 0.0F; // [0, 1)
    float lfoValue = 0.0F; // [-1, 1]
    bool valid = false;    // audio thread published at least once
};

// Publishes the effective (modulated) rhythm plus LFO phase/waveform so the
// MAIN and EDIT screens can show the same movement the audio thread hears,
// instead of the stale base values held by the UI-side Sequencer.
class ModulationMirror {
public:
    static constexpr std::size_t Tracks = ModulationTrackCount;

    void publish(std::size_t track,
                 const EffectiveRhythm& rhythm,
                 float lfoPhase,
                 float lfoValue) noexcept {
        if (track >= Tracks) {
            return;
        }
        rhythm_[track].store(packRhythm(rhythm), std::memory_order_relaxed);
        phase_[track].store(floatBits(lfoPhase), std::memory_order_relaxed);
        value_[track].store(floatBits(lfoValue), std::memory_order_relaxed);
    }

    LiveModulationState read(std::size_t track) const noexcept {
        LiveModulationState state;
        if (track >= Tracks) {
            return state;
        }
        const auto packed = rhythm_[track].load(std::memory_order_relaxed);
        if (packed == 0) {
            return state;
        }
        state.rhythm = {
            static_cast<std::uint8_t>(packed >> 16),
            static_cast<std::uint8_t>(packed >> 8),
            static_cast<std::uint8_t>(packed),
        };
        state.lfoPhase = bitsFloat(phase_[track].load(std::memory_order_relaxed));
        state.lfoValue = bitsFloat(value_[track].load(std::memory_order_relaxed));
        state.valid = true;
        return state;
    }

    void reset() noexcept {
        for (std::size_t track = 0; track < Tracks; ++track) {
            rhythm_[track].store(0, std::memory_order_relaxed);
            phase_[track].store(0, std::memory_order_relaxed);
            value_[track].store(0, std::memory_order_relaxed);
        }
    }

    bool isLockFree() const noexcept {
        return rhythm_[0].is_lock_free() && phase_[0].is_lock_free() &&
               value_[0].is_lock_free();
    }

private:
    // Zero is reserved for "never published"; a valid rhythm always has a
    // non-zero length so the packed value can never collide with it.
    static std::uint32_t packRhythm(const EffectiveRhythm& rhythm) noexcept {
        if (rhythm.length == 0 || rhythm.fill == 0 ||
            rhythm.fill > rhythm.length) {
            return 0;
        }
        return (static_cast<std::uint32_t>(rhythm.length) << 16) |
               (static_cast<std::uint32_t>(rhythm.fill) << 8) |
               static_cast<std::uint32_t>(rhythm.offset);
    }

    static std::uint32_t floatBits(float value) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    static float bitsFloat(std::uint32_t bits) noexcept {
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::array<std::atomic<std::uint32_t>, Tracks> rhythm_{};
    std::array<std::atomic<std::uint32_t>, Tracks> phase_{};
    std::array<std::atomic<std::uint32_t>, Tracks> value_{};
};

} // namespace pocket::ui
