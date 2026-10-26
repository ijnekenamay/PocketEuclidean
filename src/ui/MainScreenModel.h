#pragma once

#include "sequencer/Sequencer.h"
#include "ui/ModulationMirror.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket::ui {

struct TrackAudioSettings;

struct MainTrackSnapshot {
    EuclideanConfig rhythm{};
    std::array<bool, EuclideanGenerator::MaxLength> activeSteps{};
    std::uint8_t position = 0;
    InstrumentType instrument = InstrumentType::Sampler;
    bool muted = false;
    bool selected = false;
};

struct MainScreenSnapshot {
    std::array<MainTrackSnapshot, TrackCount> tracks{};
    double tempoBpm = 120.0;
    std::uint64_t masterTickPosition = 0;
    bool playing = false;
};

// Optional live overlay: while the audio thread is playing, the UI-side
// Sequencer keeps base rhythm values, so the effective (modulated) rhythm must
// come from the audio mirror instead.
struct MainScreenLiveModulation {
    const ModulationMirror* mirror = nullptr;
    bool active = false;
};

bool makeMainScreenSnapshot(const Sequencer& sequencer,
                            std::size_t selectedTrack,
                            MainScreenSnapshot& snapshot);

bool makeMainScreenSnapshot(
    const Sequencer& sequencer,
    std::size_t selectedTrack,
    const std::array<TrackAudioSettings, TrackCount>& audioTrackSettings,
    MainScreenSnapshot& snapshot);

bool makeMainScreenSnapshot(
    const Sequencer& sequencer,
    std::size_t selectedTrack,
    const std::array<TrackAudioSettings, TrackCount>& audioTrackSettings,
    const MainScreenLiveModulation& live,
    MainScreenSnapshot& snapshot);

} // namespace pocket::ui
