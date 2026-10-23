#pragma once

#include "ui/MainScreenLayout.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket::ui {

enum class MainDrawCommandType : std::uint8_t {
    Clear,
    Text,
    StepBar
};

enum class MainTextRole : std::uint8_t {
    Tempo,
    Transport,
    Track
};

struct MainDrawCommand {
    MainDrawCommandType type = MainDrawCommandType::Clear;
    MainTextRole textRole = MainTextRole::Tempo;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    RgbaColor color{};
    double numericValue = 0.0;
    std::uint64_t masterTickPosition = 0;
    std::uint8_t trackNumber = 0;
    InstrumentType instrument = InstrumentType::Sampler;
    bool active = false;
    bool playing = false;
    bool muted = false;
    bool selected = false;
    bool next = false;
};

constexpr std::size_t MainScreenMaxDrawCommands =
    3 + TrackCount * (1 + EuclideanGenerator::MaxLength);
static_assert(MainScreenMaxDrawCommands == 267,
              "Update the documented main-screen rendering budget");

struct MainScreenDrawList {
    std::array<MainDrawCommand, MainScreenMaxDrawCommands> commands{};
    std::size_t commandCount = 0;
};

bool buildMainScreenDrawList(const MainScreenFrame& frame,
                             MainScreenDrawList& drawList) noexcept;

} // namespace pocket::ui
