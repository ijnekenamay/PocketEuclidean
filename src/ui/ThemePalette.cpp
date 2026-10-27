#include "ui/ThemePalette.h"

#include <array>

namespace pocket::ui {
namespace {

constexpr std::array<ThemeDefinition, 8> Themes{{
    {platform::ThemeId::QuietDark,
     "Quiet Dark",
     {{16, 19, 19, 255},
      {230, 233, 227, 255},
      {104, 115, 109, 255},
      {163, 201, 178, 255}}},
    {platform::ThemeId::WarmPaper,
     "Warm Paper",
     {{244, 241, 232, 255},
      {35, 39, 36, 255},
      {126, 125, 115, 255},
      {48, 105, 83, 255}}},
    {platform::ThemeId::HighContrast,
     "High Contrast",
     {{0, 0, 0, 255},
      {255, 255, 255, 255},
      {176, 176, 176, 255},
      {255, 224, 64, 255}}},
    // Retro-computer presets: near-black backgrounds with saturated RGB
    // phosphors. Each keeps the same four semantic roles so every screen
    // switches as one piece without per-widget color overrides.
    {platform::ThemeId::NeonRgb,
     "Neon RGB",
     {{4, 6, 12, 255},
      {0, 240, 255, 255},
      {52, 84, 120, 255},
      {255, 40, 140, 255}}},
    {platform::ThemeId::Outrun,
     "Outrun",
     {{26, 8, 46, 255},
      {120, 224, 255, 255},
      {104, 76, 150, 255},
      {255, 72, 184, 255}}},
    {platform::ThemeId::AmberCrt,
     "Amber CRT",
     {{10, 6, 0, 255},
      {255, 176, 0, 255},
      {128, 82, 0, 255},
      {255, 232, 120, 255}}},
    {platform::ThemeId::Phosphor,
     "Phosphor",
     {{0, 8, 2, 255},
      {80, 255, 120, 255},
      {32, 112, 56, 255},
      {190, 255, 120, 255}}},
    {platform::ThemeId::VgaDos,
     "VGA DOS",
     {{0, 0, 42, 255},
      {224, 224, 224, 255},
      {72, 92, 168, 255},
      {255, 216, 0, 255}}},
}};

std::size_t indexFor(platform::ThemeId theme) noexcept {
    for (std::size_t index = 0; index < Themes.size(); ++index) {
        if (Themes[index].id == theme) {
            return index;
        }
    }
    return Themes.size();
}

} // namespace

std::size_t themeCount() noexcept {
    return Themes.size();
}

const ThemeDefinition* themeAt(std::size_t index) noexcept {
    return index < Themes.size() ? &Themes[index] : nullptr;
}

const ThemeDefinition* themeFor(platform::ThemeId theme) noexcept {
    const auto index = indexFor(theme);
    return index < Themes.size() ? &Themes[index] : nullptr;
}

std::optional<platform::ThemeId> adjacentTheme(platform::ThemeId current,
                                               int direction) noexcept {
    const auto currentIndex = indexFor(current);
    if (currentIndex == Themes.size() || direction == 0) {
        return std::nullopt;
    }

    const auto nextIndex =
        direction > 0
            ? (currentIndex + 1) % Themes.size()
            : (currentIndex + Themes.size() - 1) % Themes.size();
    return Themes[nextIndex].id;
}

const ThemePalette* paletteFor(platform::ThemeId theme) noexcept {
    const auto* definition = themeFor(theme);
    return definition == nullptr ? nullptr : &definition->palette;
}

} // namespace pocket::ui
