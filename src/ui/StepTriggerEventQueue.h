#pragma once

#include "sequencer/Sequencer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pocket::ui {

struct StepTriggerEvent {
    std::uint64_t audioFrame = 0;
    std::uint64_t masterTickPosition = 0;
    std::array<std::uint8_t, TrackCount> stepPositions{};
    std::uint8_t triggeredMask = 0;
};

class StepTriggerEventQueue {
public:
    static constexpr std::uint32_t Capacity = 32;

    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Queue capacity must be a power of two");
    static_assert(TrackCount <= 8,
                  "The trigger mask can represent at most eight tracks");

    bool pushTick(std::uint64_t audioFrame,
                  const TickResult& tick) noexcept {
        if (!tick.valid) {
            return false;
        }

        StepTriggerEvent event;
        event.audioFrame = audioFrame;
        event.masterTickPosition = tick.masterTickPosition;
        event.stepPositions = tick.stepPositions;
        for (std::size_t track = 0; track < TrackCount; ++track) {
            if (tick.triggered[track]) {
                event.triggeredMask |= static_cast<std::uint8_t>(1U << track);
            }
        }
        if (event.triggeredMask == 0) {
            return false;
        }

        return push(event);
    }

    bool pop(StepTriggerEvent& event) noexcept {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        if (read == write) {
            return false;
        }

        event = events_[read & (Capacity - 1)];
        readIndex_.store(read + 1, std::memory_order_release);
        return true;
    }

    void reset() noexcept {
        readIndex_.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
        droppedCount_.store(0, std::memory_order_relaxed);
    }

    std::uint32_t droppedCount() const noexcept {
        return droppedCount_.load(std::memory_order_relaxed);
    }

    bool isLockFree() const noexcept {
        return writeIndex_.is_lock_free() && readIndex_.is_lock_free() &&
               droppedCount_.is_lock_free();
    }

private:
    bool push(const StepTriggerEvent& event) noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        if (write - read >= Capacity) {
            droppedCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        events_[write & (Capacity - 1)] = event;
        writeIndex_.store(write + 1, std::memory_order_release);
        return true;
    }

    std::array<StepTriggerEvent, Capacity> events_{};
    alignas(64) std::atomic<std::uint32_t> writeIndex_{0};
    alignas(64) std::atomic<std::uint32_t> readIndex_{0};
    std::atomic<std::uint32_t> droppedCount_{0};
};

} // namespace pocket::ui
