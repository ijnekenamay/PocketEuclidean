#pragma once
#include "sequencer/Sequencer.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace pocket::ui {

struct PlayheadState {
    std::uint8_t position = 0;
    std::uint8_t length = 0;   // 0 = まだ未公開
};

// 音声スレッドが書き、UIスレッドが読む。最新値だけ必要なのでキューは使わない。
class PlayheadMirror {
public:
    void publish(std::size_t track, std::uint8_t position,
                 std::uint8_t length) noexcept {
        if (track < TrackCount) {
            slots_[track].store(
                static_cast<std::uint16_t>((position << 8) | length),
                std::memory_order_relaxed);
        }
    }
    PlayheadState read(std::size_t track) const noexcept {
        if (track >= TrackCount) return {};
        const auto v = slots_[track].load(std::memory_order_relaxed);
        return {static_cast<std::uint8_t>(v >> 8),
                static_cast<std::uint8_t>(v & 0xFF)};
    }
    bool isLockFree() const noexcept {
        return slots_[0].is_lock_free();
    }
private:
    std::array<std::atomic<std::uint16_t>, TrackCount> slots_{};
};

} // namespace pocket::ui