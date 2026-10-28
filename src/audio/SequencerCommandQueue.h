#pragma once

#include "sequencer/Sequencer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pocket::audio {

enum class SequencerCommandType : std::uint8_t {
    Start,
    Stop,
    Continue,
    SetTempo,
    SetRhythm,
    SetLfo,
    SetJitter,
    SetTuring,
    ReseedTuring,
    SetMuted,
    SetInstrument,
    SetVolume,
    SetPan,
    SetReverbSend,
    SetDelaySend,
    SetEnvelope,
    SetFilter,
    SetSynth
};

// Plain-data edit sent from the UI thread to the audio thread's Sequencer.
struct SequencerCommand {
    SequencerCommandType type = SequencerCommandType::Stop;
    std::uint8_t track = 0;
    std::uint8_t lfoIndex = 0;
    double tempo = 120.0;
    EuclideanConfig rhythm{};
    LfoConfig lfo{};
    JitterConfig jitter{};
    TuringConfig turing{};
    bool muted = false;
    InstrumentType instrument = InstrumentType::Sampler;
    std::uint8_t volume = 100;
    float pan = 0.0F;
    float reverbSend = 0.0F;
    float delaySend = 0.0F;
    AdEnvelopeSettings envelope{};
    TrackFilterSettings filter{};
    TrackSynthSettings synth{};
};

// Single-producer (UI) / single-consumer (audio) lock-free queue.
class SequencerCommandQueue {
public:
    static constexpr std::size_t Capacity = 256;

    bool push(const SequencerCommand& command) noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        if (write - read >= Capacity) {
            return false;
        }
        commands_[write % Capacity] = command;
        writeIndex_.store(write + 1, std::memory_order_release);
        return true;
    }

    bool pop(SequencerCommand& command) noexcept {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        if (read == write) {
            return false;
        }
        command = commands_[read % Capacity];
        readIndex_.store(read + 1, std::memory_order_release);
        return true;
    }

    bool isLockFree() const noexcept {
        return writeIndex_.is_lock_free() && readIndex_.is_lock_free();
    }

private:
    std::array<SequencerCommand, Capacity> commands_{};
    alignas(64) std::atomic<std::uint32_t> writeIndex_{0};
    alignas(64) std::atomic<std::uint32_t> readIndex_{0};
};

// Audio-thread side. Allocation-free; invalid commands are rejected.
inline bool applySequencerCommand(Sequencer& sequencer,
                                  const SequencerCommand& c) noexcept {
    switch (c.type) {
    case SequencerCommandType::Start:
        sequencer.restart();
        return true;
    case SequencerCommandType::Stop:
        sequencer.stop();
        return true;
    case SequencerCommandType::Continue:
        sequencer.resume();
        return true;
    case SequencerCommandType::SetTempo:
        return sequencer.setTempo(c.tempo);
    case SequencerCommandType::SetRhythm:
        return sequencer.setRhythm(c.track, c.rhythm);
    case SequencerCommandType::SetLfo:
        return sequencer.setLfo(c.track, c.lfo, c.lfoIndex);
    case SequencerCommandType::SetJitter:
        return sequencer.setJitter(c.track, c.jitter);
    case SequencerCommandType::SetTuring:
        return sequencer.setTuring(c.track, c.turing);
    case SequencerCommandType::ReseedTuring:
        return sequencer.reseedTuring(c.track);
    case SequencerCommandType::SetMuted:
        return sequencer.setMuted(c.track, c.muted);
    case SequencerCommandType::SetInstrument:
        return sequencer.setInstrument(c.track, c.instrument);
    case SequencerCommandType::SetVolume:
        return sequencer.setVolume(c.track, c.volume);
    case SequencerCommandType::SetPan:
        return sequencer.setPan(c.track, c.pan);
    case SequencerCommandType::SetReverbSend:
        return sequencer.setReverbSend(c.track, c.reverbSend);
    case SequencerCommandType::SetDelaySend:
        return sequencer.setDelaySend(c.track, c.delaySend);
    case SequencerCommandType::SetEnvelope:
        return sequencer.setEnvelope(c.track, c.envelope);
    case SequencerCommandType::SetFilter:
        return sequencer.setFilter(c.track, c.filter);
    case SequencerCommandType::SetSynth:
        return sequencer.setSynth(c.track, c.synth);
    }
    return false;
}

// UI-thread side. Compares the UI's Sequencer against what was last sent and
// pushes only the differences. A full queue leaves the shadow untouched, so
// the change is retried on the next sync().
class SequencerSync {
public:
    SequencerSync(SequencerCommandQueue& queue, const Sequencer& initial)
        : queue_(queue),
          shadow_(initial),
          transportRevision_(initial.transportRevision()) {}

    void sync(const Sequencer& ui) {
        if (ui.tempo() != shadow_.tempo()) {
            SequencerCommand c;
            c.type = SequencerCommandType::SetTempo;
            c.tempo = ui.tempo();
            if (queue_.push(c)) {
                shadow_.setTempo(c.tempo);
            }
        }
        for (std::size_t t = 0; t < TrackCount; ++t) {
            syncTrack(t, ui.track(t));
        }
        if (ui.transportRevision() != transportRevision_) {
            SequencerCommand c;
            if (!ui.isPlaying()) {
                c.type = SequencerCommandType::Stop;
            } else if (ui.lastStartWasResume()) {
                c.type = SequencerCommandType::Continue;
            } else {
                c.type = SequencerCommandType::Start;
            }
            if (queue_.push(c)) {
                if (c.type == SequencerCommandType::Start) {
                    shadow_.restart();
                } else if (c.type == SequencerCommandType::Continue) {
                    shadow_.resume();
                } else {
                    shadow_.stop();
                }
                transportRevision_ = ui.transportRevision();
            }
        }
    }

private:
    static bool same(const EuclideanConfig& a, const EuclideanConfig& b) {
        return a.length == b.length && a.fill == b.fill && a.offset == b.offset;
    }
    static bool same(const LfoConfig& a, const LfoConfig& b) {
        return a.enabled == b.enabled && a.waveform == b.waveform &&
               a.rateMode == b.rateMode && a.rateHz == b.rateHz &&
               a.division == b.division && a.destination == b.destination &&
               a.amount == b.amount;
    }
    static bool same(const JitterConfig& a, const JitterConfig& b) {
        return a.enabled == b.enabled && a.mode == b.mode &&
               a.intensity == b.intensity && a.rate == b.rate &&
               a.shiftRange == b.shiftRange &&
               a.useCustomSeed == b.useCustomSeed &&
               a.randomSeed == b.randomSeed;
    }
    static bool same(const TuringConfig& a, const TuringConfig& b) {
        return a.enabled == b.enabled && a.clock == b.clock &&
               a.rate == b.rate && a.length == b.length &&
               a.chance == b.chance && a.range == b.range &&
               a.scale == b.scale && a.root == b.root &&
               a.glide == b.glide && a.drift == b.drift &&
               a.locked == b.locked &&
               a.useCustomSeed == b.useCustomSeed &&
               a.randomSeed == b.randomSeed;
    }
    static bool same(const AdEnvelopeSettings& a, const AdEnvelopeSettings& b) {
        return a.attackSeconds == b.attackSeconds &&
               a.decaySeconds == b.decaySeconds;
    }
    static bool same(const TrackFilterSettings& a,
                     const TrackFilterSettings& b) {
        return a.enabled == b.enabled && a.cutoffHz == b.cutoffHz;
    }
    static bool same(const TrackSynthSettings& a,
                     const TrackSynthSettings& b) {
        return a.engineType == b.engineType && a.model == b.model &&
               a.pitchHz == b.pitchHz &&
               a.morph == b.morph && a.timbre == b.timbre &&
               a.color == b.color;
    }

    void syncTrack(std::size_t t, const TrackState& now) {
        const auto& old = shadow_.track(t);
        SequencerCommand c;
        c.track = static_cast<std::uint8_t>(t);

        if (!same(now.rhythm, old.rhythm)) {
            c.type = SequencerCommandType::SetRhythm;
            c.rhythm = now.rhythm;
            if (queue_.push(c)) {
                shadow_.setRhythm(t, now.rhythm);
            }
        }
        for (std::size_t i = 0; i < MaxLfosPerTrack; ++i) {
            if (!same(now.lfos[i], old.lfos[i])) {
                c.type = SequencerCommandType::SetLfo;
                c.lfoIndex = static_cast<std::uint8_t>(i);
                c.lfo = now.lfos[i];
                if (queue_.push(c)) {
                    shadow_.setLfo(t, c.lfo, i);
                }
            }
        }
        if (!same(now.jitter, old.jitter)) {
            c.type = SequencerCommandType::SetJitter;
            c.jitter = now.jitter;
            if (queue_.push(c)) {
                shadow_.setJitter(t, now.jitter);
            }
        }
        if (!same(now.turing, old.turing)) {
            c.type = SequencerCommandType::SetTuring;
            c.turing = now.turing;
            if (queue_.push(c)) {
                shadow_.setTuring(t, now.turing);
            }
        }
        if (now.turingReseedRevision != old.turingReseedRevision) {
            c.type = SequencerCommandType::ReseedTuring;
            if (queue_.push(c)) {
                shadow_.reseedTuring(t);
            }
        }
        if (now.muted != old.muted) {
            c.type = SequencerCommandType::SetMuted;
            c.muted = now.muted;
            if (queue_.push(c)) {
                shadow_.setMuted(t, now.muted);
            }
        }
        if (now.instrument != old.instrument) {
            c.type = SequencerCommandType::SetInstrument;
            c.instrument = now.instrument;
            if (queue_.push(c)) {
                shadow_.setInstrument(t, now.instrument);
            }
        }
        if (now.volume != old.volume) {
            c.type = SequencerCommandType::SetVolume;
            c.volume = now.volume;
            if (queue_.push(c)) {
                shadow_.setVolume(t, now.volume);
            }
        }
        if (now.pan != old.pan) {
            c.type = SequencerCommandType::SetPan;
            c.pan = now.pan;
            if (queue_.push(c)) {
                shadow_.setPan(t, now.pan);
            }
        }
        if (now.reverbSend != old.reverbSend) {
            c.type = SequencerCommandType::SetReverbSend;
            c.reverbSend = now.reverbSend;
            if (queue_.push(c)) {
                shadow_.setReverbSend(t, now.reverbSend);
            }
        }
        if (now.delaySend != old.delaySend) {
            c.type = SequencerCommandType::SetDelaySend;
            c.delaySend = now.delaySend;
            if (queue_.push(c)) {
                shadow_.setDelaySend(t, now.delaySend);
            }
        }
        if (!same(now.envelope, old.envelope)) {
            c.type = SequencerCommandType::SetEnvelope;
            c.envelope = now.envelope;
            if (queue_.push(c)) {
                shadow_.setEnvelope(t, now.envelope);
            }
        }
        if (!same(now.filter, old.filter)) {
            c.type = SequencerCommandType::SetFilter;
            c.filter = now.filter;
            if (queue_.push(c)) {
                shadow_.setFilter(t, now.filter);
            }
        }
        if (!same(now.synth, old.synth)) {
            c.type = SequencerCommandType::SetSynth;
            c.synth = now.synth;
            if (queue_.push(c)) {
                shadow_.setSynth(t, now.synth);
            }
        }
    }

    SequencerCommandQueue& queue_;
    Sequencer shadow_;
    std::uint32_t transportRevision_ = 0;
};

} // namespace pocket::audio
