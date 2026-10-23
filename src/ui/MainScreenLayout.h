#pragma once

#include "ui/MainScreenModel.h"
#include "ui/ThemePalette.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket::ui {

struct StepBarGeometry {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool active = false;
    bool next = false;
};

struct TrackRowGeometry {
    std::int32_t y = 0;
    std::int32_t height = 0;
    std::uint8_t trackNumber = 0;
    std::uint8_t stepCount = 0;
    bool selected = false;
    bool muted = false;
    InstrumentType instrument = InstrumentType::Sampler;
    std::array<StepBarGeometry, EuclideanGenerator::MaxLength> steps{};
};

struct MainScreenFrame {
    ThemePalette palette{};
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t tempoX = 0;
    std::int32_t tempoY = 0;
    std::int32_t patternX = 0;
    std::int32_t rowPitch = 0;
    std::int32_t stepPitch = 0;
    double tempoBpm = 120.0;
    std::uint64_t masterTickPosition = 0;
    bool playing = false;
    std::array<TrackRowGeometry, TrackCount> tracks{};
};

bool layoutMainScreen(const MainScreenSnapshot& snapshot,
                      platform::ThemeId theme,
                      std::int32_t width,
                      std::int32_t height,
                      MainScreenFrame& frame) noexcept;

} // namespace pocket::ui
