#include "sequencer/Sequencer.h"

#include <cmath>
#include <stdexcept>

namespace pocket {

bool Sequencer::setTempo(double bpm) noexcept {
    if (!std::isfinite(bpm) || bpm < MinTempoBpm || bpm > MaxTempoBpm) {
        return false;
    }
    tempoBpm_ = bpm;
    return true;
}

void Sequencer::start() noexcept {
    if (!playing_) {
        reset();
        playing_ = true;
    }
    ++transportRevision_;
    lastStartWasResume_ = false;
}

void Sequencer::restart() noexcept {
    reset();
    playing_ = true;
    ++transportRevision_;
    lastStartWasResume_ = false;
}

void Sequencer::resume() noexcept {
    if (!playing_) {
        playing_ = true;
        ++transportRevision_;
        lastStartWasResume_ = true;
    }
}

void Sequencer::stop() noexcept {
    if (playing_) {
        ++transportRevision_;
    }
    playing_ = false;
    secondsUntilNextTick_ = 0.0;
}

void Sequencer::reset() noexcept {
    for (auto& trackState : tracks_) {
        trackState.position = 0;
    }
    secondsUntilNextTick_ = 0.0;
    masterTickPosition_ = 0;
    modulation_.reset();
    std::array<JitterConfig, TrackCount> jitterConfigs{};
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        jitterConfigs[i] = tracks_[i].jitter;
    }
    jitter_.reset(jitterConfigs);
    // Keep the S&H registers so a live reseed survives stop/start, but rewind
    // the loop and drift so every take begins on step zero.
    turing_.resetTransport();
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        // Re-read the head so the first note already carries the phrase's
        // pitch even in EUCLID/FREE modes, where the next clock may be late.
        turing_.configure(i, tracks_[i].turing);
    }
}

const TrackState& Sequencer::track(std::size_t index) const {
    if (index >= TrackCount) {
        throw std::out_of_range("Track index is outside the sequencer");
    }
    return tracks_[index];
}

bool Sequencer::setRhythm(std::size_t index, const EuclideanConfig& config) {
    if (index >= TrackCount) {
        return false;
    }
    if (!EuclideanGenerator::isValid(config)) {
        return false;
    }
    tracks_[index].rhythm = config;
    tracks_[index].effectiveRhythm = {
        config.length, config.fill, config.offset};
    tracks_[index].position %= config.length;
    return true;
}

bool Sequencer::setLfo(std::size_t trackIndex,
                       const LfoConfig& config,
                       std::size_t lfoIndex) noexcept {
    if (trackIndex >= TrackCount || lfoIndex >= MaxLfosPerTrack ||
        !ModulationEngine::isValid(config)) {
        return false;
    }
    tracks_[trackIndex].lfos[lfoIndex] = config;
    return true;
}

bool Sequencer::setJitter(std::size_t trackIndex,
                          const JitterConfig& config) noexcept {
    if (trackIndex >= TrackCount || !JitterEngine::isValid(config)) {
        return false;
    }
    auto& current = tracks_[trackIndex].jitter;
    const bool seedChanged = current.useCustomSeed != config.useCustomSeed ||
                             current.randomSeed != config.randomSeed;
    current = config;
    if (seedChanged) {
        jitter_.resetTrack(trackIndex, config);
    } else {
        jitter_.invalidate(trackIndex);
    }
    return true;
}

bool Sequencer::setTuring(std::size_t trackIndex,
                          const TuringConfig& config) noexcept {
    if (trackIndex >= TrackCount || !TuringEngine::isValid(config)) {
        return false;
    }
    auto& current = tracks_[trackIndex].turing;
    const bool seedChanged = current.useCustomSeed != config.useCustomSeed ||
                             current.randomSeed != config.randomSeed;
    current = config;
    if (seedChanged) {
        turing_.resetTrack(trackIndex, config);
    } else {
        turing_.configure(trackIndex, config);
    }
    return true;
}

bool Sequencer::reseedTuring(std::size_t trackIndex) noexcept {
    if (trackIndex >= TrackCount) {
        return false;
    }
    turing_.reseed(trackIndex, tracks_[trackIndex].turing);
    ++tracks_[trackIndex].turingReseedRevision;
    return true;
}

void Sequencer::reseedAllTuring() noexcept {
    for (std::size_t track = 0; track < TrackCount; ++track) {
        (void)reseedTuring(track);
    }
}

TuringDisplayState Sequencer::turingDisplay(
    std::size_t trackIndex) const noexcept {
    if (trackIndex >= TrackCount) {
        return {};
    }
    return turing_.display(trackIndex, tracks_[trackIndex].turing);
}

std::uint32_t Sequencer::jitterEventsMask(std::size_t trackIndex,
                                          bool& valid) const noexcept {
    valid = false;
    if (trackIndex >= TrackCount || !tracks_[trackIndex].jitter.enabled) {
        return 0;
    }
    return jitter_.eventMask(trackIndex, valid);
}

bool Sequencer::setMuted(std::size_t index, bool muted) {
    if (index >= TrackCount) {
        return false;
    }
    tracks_[index].muted = muted;
    return true;
}

bool Sequencer::setInstrument(std::size_t index, InstrumentType instrument) {
    if (index >= TrackCount) {
        return false;
    }
    tracks_[index].instrument = instrument;
    return true;
}

bool Sequencer::setVolume(std::size_t index, std::uint8_t volume) {
    if (index >= TrackCount || volume > 100) {
        return false;
    }
    tracks_[index].volume = volume;
    return true;
}

bool Sequencer::setPan(std::size_t index, float normalizedPan) noexcept {
    if (index >= TrackCount || !std::isfinite(normalizedPan) ||
        normalizedPan < -1.0F || normalizedPan > 1.0F) {
        return false;
    }
    tracks_[index].pan = normalizedPan;
    return true;
}

bool Sequencer::setReverbSend(std::size_t index, float amount) noexcept {
    if (index >= TrackCount || !std::isfinite(amount) || amount < 0.0F ||
        amount > 1.0F) {
        return false;
    }
    tracks_[index].reverbSend = amount;
    return true;
}

bool Sequencer::setDelaySend(std::size_t index, float amount) noexcept {
    if (index >= TrackCount || !std::isfinite(amount) || amount < 0.0F ||
        amount > 1.0F) {
        return false;
    }
    tracks_[index].delaySend = amount;
    return true;
}

bool Sequencer::setEnvelope(
    std::size_t index, const AdEnvelopeSettings& envelope) noexcept {
    if (index >= TrackCount || !std::isfinite(envelope.attackSeconds) ||
        envelope.attackSeconds < AdEnvelopeSettings::MinAttackSeconds ||
        envelope.attackSeconds > AdEnvelopeSettings::MaxAttackSeconds ||
        !std::isfinite(envelope.decaySeconds) ||
        envelope.decaySeconds < AdEnvelopeSettings::MinDecaySeconds ||
        envelope.decaySeconds > AdEnvelopeSettings::MaxDecaySeconds) {
        return false;
    }
    tracks_[index].envelope = envelope;
    return true;
}

bool Sequencer::setFilter(
    std::size_t index, const TrackFilterSettings& filter) noexcept {
    if (index >= TrackCount || !std::isfinite(filter.cutoffHz) ||
        filter.cutoffHz < TrackFilterSettings::MinCutoffHz ||
        filter.cutoffHz > TrackFilterSettings::MaxCutoffHz) {
        return false;
    }
    tracks_[index].filter = filter;
    return true;
}

bool Sequencer::setSynth(
    std::size_t index, const TrackSynthSettings& synth) noexcept {
    if (index >= TrackCount || !std::isfinite(synth.pitchHz) ||
        synth.pitchHz < TrackSynthSettings::MinPitchHz ||
        synth.pitchHz > TrackSynthSettings::MaxPitchHz ||
        synth.model >= TrackSynthSettings::ModelCount ||
        !std::isfinite(synth.morph) || synth.morph < 0.0F ||
        synth.morph > 1.0F || !std::isfinite(synth.timbre) ||
        synth.timbre < 0.0F || synth.timbre > 1.0F ||
        !std::isfinite(synth.color) || synth.color < 0.0F ||
        synth.color > 1.0F) {
        return false;
    }
    tracks_[index].synth = synth;
    return true;
}

TickResult Sequencer::tick() {
    TickResult result;
    if (!playing_) {
        return result;
    }

    for (const auto& trackState : tracks_) {
        for (const auto& lfo : trackState.lfos) {
            if (!ModulationEngine::isValid(lfo)) {
                result.valid = false;
                return result;
            }
        }
        if (!JitterEngine::isValid(trackState.jitter)) {
            result.valid = false;
            return result;
        }
        if (!TuringEngine::isValid(trackState.turing)) {
            result.valid = false;
            return result;
        }
    }

    result.masterTickPosition = masterTickPosition_++;
    const double secondsPerTick = 60.0 / (tempoBpm_ * 4.0);
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        auto& trackState = tracks_[i];
        const EffectiveRhythm base{
            trackState.rhythm.length,
            trackState.rhythm.fill,
            trackState.rhythm.offset};
        const auto effective = modulation_.advance(
            i, trackState.lfos, base, secondsPerTick, tempoBpm_);
        if (!effective) {
            result.valid = false;
            return result;
        }
        trackState.effectiveRhythm = *effective;
        trackState.position %= trackState.effectiveRhythm.length;
        result.stepPositions[i] = trackState.position;
        const EuclideanConfig effectivePattern{
            trackState.effectiveRhythm.length,
            trackState.effectiveRhythm.fill,
            trackState.effectiveRhythm.offset};
        const auto trigger = jitter_.process(
            i, trackState.jitter, effectivePattern, trackState.position,
            result.masterTickPosition);
        if (!trigger) {
            result.valid = false;
            return result;
        }
        result.triggered[i] = *trigger;
        const auto pitch = turing_.process(
            i, trackState.turing, *trigger, result.masterTickPosition,
            secondsPerTick);
        if (!pitch) {
            result.valid = false;
            return result;
        }
        result.pitchOffsets[i] = *pitch;
        trackState.position = static_cast<std::uint8_t>(
            (trackState.position + 1) % trackState.effectiveRhythm.length);
    }
    return result;
}

} // namespace pocket
