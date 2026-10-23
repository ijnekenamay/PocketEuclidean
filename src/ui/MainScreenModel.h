#pragma once

#include "sequencer/Sequencer.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket::ui {

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

bool makeMainScreenSnapshot(const Sequencer& sequencer,
                            std::size_t selectedTrack,
                            MainScreenSnapshot& snapshot);

} // namespace pocket::ui
