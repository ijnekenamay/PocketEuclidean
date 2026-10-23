#include "platform/SystemSettings.h"

#include <cmath>

namespace pocket::platform {

bool isCategoryAvailable(SettingsCategory category,
                         const SystemCapabilities& capabilities) noexcept {
    switch (category) {
    case SettingsCategory::ClockAndTransport:
        return true;
    case SettingsCategory::Midi:
        return capabilities.midi;
    case SettingsCategory::Audio:
        return capabilities.audioConfiguration;
    case SettingsCategory::InputAndControls:
        return capabilities.inputConfiguration;
    case SettingsCategory::Display:
        return capabilities.displayConfiguration;
    case SettingsCategory::ProjectAndStorage:
        return capabilities.projectStorage;
    case SettingsCategory::AboutAndDiagnostics:
        return capabilities.diagnostics;
    }
    return false;
}

bool isValid(const SystemSettings& settings) noexcept {
    const auto theme = static_cast<std::uint8_t>(settings.display.theme);
    return settings.formatVersion == SystemSettingsFormatVersion &&
           std::isfinite(settings.clock.initialTempoBpm) &&
           settings.clock.initialTempoBpm >= 40.0 &&
           settings.clock.initialTempoBpm <= 240.0 &&
           settings.midi.inputChannel >= 1 && settings.midi.inputChannel <= 16 &&
           settings.midi.outputChannel >= 1 && settings.midi.outputChannel <= 16 &&
           settings.audio.sampleRate >= 8000 && settings.audio.sampleRate <= 192000 &&
           settings.audio.bufferFrames >= 32 && settings.audio.bufferFrames <= 4096 &&
           settings.audio.masterVolumePercent <= 100 &&
           settings.input.repeatDelayMs >= 50 &&
           settings.input.repeatIntervalMs >= 10 &&
           settings.display.width >= 320 && settings.display.height >= 240 &&
           settings.display.scale >= 1 &&
           theme <= static_cast<std::uint8_t>(ThemeId::HighContrast);
}

} // namespace pocket::platform
