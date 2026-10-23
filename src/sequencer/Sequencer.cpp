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
}

void Sequencer::stop() noexcept {
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
        trackState.position = static_cast<std::uint8_t>(
            (trackState.position + 1) % trackState.effectiveRhythm.length);
    }
    return result;
}

} // namespace pocket
