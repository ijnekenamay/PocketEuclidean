#pragma once

#include "platform/SystemSettings.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pocket::ui {

struct RgbaColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;
};

struct ThemePalette {
    RgbaColor background;
    RgbaColor primary;
    RgbaColor subdued;
    RgbaColor accent;
};

struct ThemeDefinition {
    platform::ThemeId id;
    std::string_view name;
    ThemePalette palette;
};

std::size_t themeCount() noexcept;
const ThemeDefinition* themeAt(std::size_t index) noexcept;
const ThemeDefinition* themeFor(platform::ThemeId theme) noexcept;
std::optional<platform::ThemeId> adjacentTheme(
    platform::ThemeId current,
    int direction) noexcept;
const ThemePalette* paletteFor(platform::ThemeId theme) noexcept;

} // namespace pocket::ui
