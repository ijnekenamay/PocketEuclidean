#pragma once

#include "sequencer/Modulation.h"
#include "sequencer/Turing.h"

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
    float lfoPhase = 0.0F;  // [0, 1)
    float lfoValue = 0.0F;  // [-1, 1]
    std::uint32_t jitterEvents = 0; // bit n = step n triggers after jitter
    bool valid = false;             // audio thread published at least once
    bool jitterValid = false;       // jitterEvents reflects the current config
};

// Snapshot of one track's S&H register. The UI redraws all 32 slots and the
// moving head marker from this, so the TURING screen follows the audio thread.
struct LiveTuringState {
    std::array<std::uint8_t, TuringRegisterLength> values{};
    std::uint8_t head = 0;
    std::int8_t semitone = 0;
    bool valid = false;
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

    // Published separately because jitter is opt-in per track and the main
    // rhythm overlay must remain valid when no jitter events exist.
    void publishJitter(std::size_t track,
                       std::uint32_t events,
                       bool valid) noexcept {
        if (track >= Tracks) {
            return;
        }
        jitter_[track].store(events, std::memory_order_relaxed);
        jitterValid_[track].store(valid, std::memory_order_relaxed);
    }

    void publishTuring(std::size_t track,
                       const TuringDisplayState& state) noexcept {
        if (track >= Tracks) {
            return;
        }
        for (std::size_t word = 0; word < TuringWords; ++word) {
            const std::size_t base = word * 4;
            const std::uint32_t packed =
                static_cast<std::uint32_t>(state.values[base]) |
                (static_cast<std::uint32_t>(state.values[base + 1]) << 8) |
                (static_cast<std::uint32_t>(state.values[base + 2]) << 16) |
                (static_cast<std::uint32_t>(state.values[base + 3]) << 24);
            turingValues_[track][word].store(packed,
                                             std::memory_order_relaxed);
        }
        turingHead_[track].store(state.head, std::memory_order_relaxed);
        turingSemitone_[track].store(
            static_cast<std::uint32_t>(
                static_cast<std::uint8_t>(state.semitone)),
            std::memory_order_relaxed);
        turingValid_[track].store(state.valid, std::memory_order_relaxed);
    }

    LiveTuringState readTuring(std::size_t track) const noexcept {
        LiveTuringState state;
        if (track >= Tracks ||
            !turingValid_[track].load(std::memory_order_relaxed)) {
            return state;
        }
        for (std::size_t word = 0; word < TuringWords; ++word) {
            const auto packed =
                turingValues_[track][word].load(std::memory_order_relaxed);
            const std::size_t base = word * 4;
            state.values[base] = static_cast<std::uint8_t>(packed);
            state.values[base + 1] =
                static_cast<std::uint8_t>(packed >> 8);
            state.values[base + 2] =
                static_cast<std::uint8_t>(packed >> 16);
            state.values[base + 3] =
                static_cast<std::uint8_t>(packed >> 24);
        }
        state.head = static_cast<std::uint8_t>(
            turingHead_[track].load(std::memory_order_relaxed));
        state.semitone = static_cast<std::int8_t>(
            turingSemitone_[track].load(std::memory_order_relaxed));
        state.valid = true;
        return state;
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
        state.jitterEvents =
            jitter_[track].load(std::memory_order_relaxed);
        state.jitterValid =
            jitterValid_[track].load(std::memory_order_relaxed);
        state.valid = true;
        return state;
    }

    void reset() noexcept {
        for (std::size_t track = 0; track < Tracks; ++track) {
            rhythm_[track].store(0, std::memory_order_relaxed);
            phase_[track].store(0, std::memory_order_relaxed);
            value_[track].store(0, std::memory_order_relaxed);
            jitter_[track].store(0, std::memory_order_relaxed);
            jitterValid_[track].store(false, std::memory_order_relaxed);
            for (auto& word : turingValues_[track]) {
                word.store(0, std::memory_order_relaxed);
            }
            turingHead_[track].store(0, std::memory_order_relaxed);
            turingSemitone_[track].store(0, std::memory_order_relaxed);
            turingValid_[track].store(false, std::memory_order_relaxed);
        }
    }

    bool isLockFree() const noexcept {
        return rhythm_[0].is_lock_free() && phase_[0].is_lock_free() &&
               value_[0].is_lock_free() && jitter_[0].is_lock_free() &&
               jitterValid_[0].is_lock_free() &&
               turingValues_[0][0].is_lock_free() &&
               turingHead_[0].is_lock_free() &&
               turingSemitone_[0].is_lock_free() &&
               turingValid_[0].is_lock_free();
    }

private:
    static constexpr std::size_t TuringWords = TuringRegisterLength / 4;

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
    std::array<std::atomic<std::uint32_t>, Tracks> jitter_{};
    std::array<std::atomic<bool>, Tracks> jitterValid_{};
    std::array<std::array<std::atomic<std::uint32_t>, TuringWords>, Tracks>
        turingValues_{};
    std::array<std::atomic<std::uint32_t>, Tracks> turingHead_{};
    std::array<std::atomic<std::uint32_t>, Tracks> turingSemitone_{};
    std::array<std::atomic<bool>, Tracks> turingValid_{};
};

} // namespace pocket::ui
