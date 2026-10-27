#pragma once

#include <cstdint>

namespace pocket::platform {

constexpr std::uint32_t SystemSettingsFormatVersion = 3;

enum class SettingsCategory : std::uint8_t {
    ClockAndTransport,
    Midi,
    Audio,
    InputAndControls,
    Display,
    ProjectAndStorage,
    AboutAndDiagnostics
};

enum class ThemeId : std::uint8_t {
    QuietDark,
    WarmPaper,
    HighContrast,
    NeonRgb,
    Outrun,
    AmberCrt,
    Phosphor,
    VgaDos
};

struct SystemCapabilities {
    bool midi = false;
    bool audioConfiguration = false;
    bool inputConfiguration = false;
    bool displayConfiguration = false;
    bool projectStorage = false;
    bool diagnostics = true;
};

bool isCategoryAvailable(SettingsCategory category,
                         const SystemCapabilities& capabilities) noexcept;

struct ClockSettings {
    double initialTempoBpm = 120.0;
};

struct MidiSettings {
    bool enabled = false;
    std::uint8_t inputChannel = 1;
    std::uint8_t outputChannel = 1;
};

struct AudioSettings {
    std::uint32_t sampleRate = 48000;
    std::uint16_t bufferFrames = 256;
    std::uint8_t masterVolumePercent = 100;
};

struct InputSettings {
    bool preferGameController = true;
    std::uint16_t repeatDelayMs = 350;
    std::uint16_t repeatIntervalMs = 120;
};

struct DisplaySettings {
    std::uint16_t width = 1024;
    std::uint16_t height = 768;
    std::uint8_t scale = 1;
    ThemeId theme = ThemeId::QuietDark;
};

struct StorageSettings {
    bool usePlatformDefaultDirectory = true;
};

struct SystemSettings {
    std::uint32_t formatVersion = SystemSettingsFormatVersion;
    ClockSettings clock{};
    MidiSettings midi{};
    AudioSettings audio{};
    InputSettings input{};
    DisplaySettings display{};
    StorageSettings storage{};
};

bool isValid(const SystemSettings& settings) noexcept;

} // namespace pocket::platform
