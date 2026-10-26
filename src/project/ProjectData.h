#pragma once

#include "sequencer/Sequencer.h"

#include <array>
#include <cstdint>

namespace pocket::project {

constexpr std::uint32_t ProjectFormatVersion = 8;
// Oldest on-disk version the reader can still load. Older files are upgraded
// in memory by filling newly added fields with their defaults.
constexpr std::uint32_t OldestSupportedProjectVersion = 6;

struct ProjectEffectState {
    std::uint16_t delayMilliseconds = 250;
    float delayFeedback = 0.25F;
    float delayMix = 0.2F;
    float filterCutoffHz = 12000.0F;
    std::uint8_t filterResponse = 0;
};

struct ProjectTrackData {
    EuclideanConfig rhythm{};
    std::array<LfoConfig, MaxLfosPerTrack> lfos{};
    JitterConfig jitter{};
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

struct ProjectData {
    std::uint32_t formatVersion = ProjectFormatVersion;
    double tempoBpm = 120.0;
    std::array<ProjectTrackData, TrackCount> tracks{};
    ProjectEffectState effects{};
};

} // namespace pocket::project
