#include "audio/AudioParameterQueue.h"

#include <cmath>

namespace pocket::audio {

bool AudioParameterQueue::push(
    const AudioParameterCommand& command) noexcept {
    if (!isValid(command)) {
        return false;
    }
    const auto write =
        writeIndex_.load(std::memory_order_relaxed);
    const auto read = readIndex_.load(std::memory_order_acquire);
    if (write - read >= Capacity) {
        return false;
    }
    commands_[write % Capacity] = command;
    writeIndex_.store(write + 1, std::memory_order_release);
    return true;
}

bool AudioParameterQueue::pop(AudioParameterCommand& command) noexcept {
    const auto read = readIndex_.load(std::memory_order_relaxed);
    const auto write = writeIndex_.load(std::memory_order_acquire);
    if (read == write) {
        return false;
    }
    command = commands_[read % Capacity];
    readIndex_.store(read + 1, std::memory_order_release);
    return true;
}

bool AudioParameterQueue::isLockFree() const noexcept {
    return writeIndex_.is_lock_free() && readIndex_.is_lock_free();
}

bool AudioParameterQueue::isValid(
    const AudioParameterCommand& command) noexcept {
    if (!std::isfinite(command.value)) {
        return false;
    }
    switch (command.parameter) {
    case AudioParameter::TrackVolume:
        return command.track < TrackCount && command.value >= 0.0F &&
               command.value <= 100.0F &&
               std::floor(command.value) == command.value;
    case AudioParameter::TrackPan:
        return command.track < TrackCount && command.value >= -1.0F &&
               command.value <= 1.0F;
    case AudioParameter::TrackMute:
    case AudioParameter::TrackFilterEnabled:
        return command.track < TrackCount &&
               (command.value == 0.0F || command.value == 1.0F);
    case AudioParameter::TrackAttack:
        return command.track < TrackCount &&
               command.value >= AdEnvelopeSettings::MinAttackSeconds &&
               command.value <= AdEnvelopeSettings::MaxAttackSeconds;
    case AudioParameter::TrackDecay:
        return command.track < TrackCount &&
               command.value >= AdEnvelopeSettings::MinDecaySeconds &&
               command.value <= AdEnvelopeSettings::MaxDecaySeconds;
    case AudioParameter::TrackFilterCutoff:
        return command.track < TrackCount &&
               command.value >= TrackFilterSettings::MinCutoffHz &&
               command.value <= TrackFilterSettings::MaxCutoffHz;
    case AudioParameter::MasterVolume:
        return command.track == 0 && command.value >= 0.0F &&
               command.value <= 100.0F &&
               std::floor(command.value) == command.value;
    }
    return false;
}

} // namespace pocket::audio
