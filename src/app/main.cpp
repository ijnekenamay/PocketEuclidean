#include "app/BitmapFont.h"
#ifdef POCKET_HAVE_SDL2
#include "app/DesktopAudioEngine.h"
#endif
#include "platform/SystemSettings.h"
#include "platform/SettingsStore.h"
#include "project/ProjectStore.h"
#include "sequencer/Sequencer.h"
#include "ui/InputMapping.h"
#include "ui/MainScreenDrawList.h"
#include "ui/MainScreenLayout.h"
#include "ui/MainScreenModel.h"
#include "ui/ThemePalette.h"
#include "ui/UiController.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef POCKET_HAVE_SDL2
#include <SDL.h>
#endif

namespace {

struct AppOptions {
    bool headless = false;
    std::int32_t width = 1024;
    std::int32_t height = 768;
    std::string capturePath;
    int startScreen = 0;
    bool startHelp = false;
    bool autoPlay = false;
    bool fullscreen = false;
    std::uint32_t audioRate = 0;
    std::uint16_t audioBuffer = 0;
    std::string samplesDir;
    std::string dataDir = "conf";
    std::int32_t captureDelayMs = 0;
};

bool parseInteger(const char* value, std::int32_t& output) {
    if (value == nullptr || *value == '\0') {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 320 || parsed > 16384) {
        return false;
    }
    output = static_cast<std::int32_t>(parsed);
    return true;
}

bool parseArguments(int argc, char** argv, AppOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--headless") {
            options.headless = true;
            continue;
        }
        if (arg == "--width" && index + 1 < argc) {
            if (!parseInteger(argv[++index], options.width)) {
                return false;
            }
            continue;
        }
        if (arg == "--height" && index + 1 < argc) {
            if (!parseInteger(argv[++index], options.height)) {
                return false;
            }
            continue;
        }
        if (arg == "--capture" && index + 1 < argc) {
            options.capturePath = argv[++index];
            continue;
        }
        if (arg == "--screen" && index + 1 < argc) {
            options.startScreen = std::atoi(argv[++index]);
            if (options.startScreen < 0 || options.startScreen > 5) {
                return false;
            }
            continue;
        }
        if (arg == "--play") {
            options.autoPlay = true;
            continue;
        }
        if (arg == "--capture-delay" && index + 1 < argc) {
            options.captureDelayMs = std::atoi(argv[++index]);
            if (options.captureDelayMs < 0) {
                return false;
            }
            continue;
        }
        if (arg == "--fullscreen") {
            options.fullscreen = true;
            continue;
        }
        if (arg == "--rate" && index + 1 < argc) {
            const long value = std::strtol(argv[++index], nullptr, 10);
            if (value < 8000 || value > 192000) {
                return false;
            }
            options.audioRate = static_cast<std::uint32_t>(value);
            continue;
        }
        if (arg == "--buffer" && index + 1 < argc) {
            const long value = std::strtol(argv[++index], nullptr, 10);
            if (value < 32 || value > 4096) {
                return false;
            }
            options.audioBuffer = static_cast<std::uint16_t>(value);
            continue;
        }
        if (arg == "--samples" && index + 1 < argc) {
            options.samplesDir = argv[++index];
            continue;
        }
        if (arg == "--data" && index + 1 < argc) {
            options.dataDir = argv[++index];
            continue;
        }
        if (arg == "--help-modal") {
            options.startHelp = true;
            continue;
        }
        if (arg == "--help") {
            std::cout
                << "PocketEuclidean app shell options:\n"
                << "  --headless        Run without SDL window\n"
                << "  --width <px>      Initial window width (320-16384)\n"
                << "  --height <px>     Initial window height (320-16384)\n"
                << "  --screen <0-5>    Start screen (Main..System)\n"
                << "  --help-modal      Start with the Help modal open\n"
                << "  --capture <file>  Render one frame to a BMP file and exit\n"
                << "  --play            Start playback immediately\n"
                << "  --samples <dir>   Load *.wav (sorted) into tracks 1-8\n"
                << "  --data <dir>      Project save directory (default ./conf)\n"
                << "                    (default: ./samples when it exists)\n"
                << "  --fullscreen      Fullscreen at the desktop/native size\n"
                << "  --rate <hz>       Requested audio rate (8000-192000)\n"
                << "  --buffer <frames> Requested audio buffer (32-4096)\n"
                << "  --capture-delay <ms>  Wait before --capture (audio runs "
                   "when combined with --play)\n";
            return false;
        }
        std::cerr << "Unknown argument: " << arg << '\n';
        return false;
    }
    return true;
}

int runHeadless(const AppOptions&) {
    pocket::Sequencer sequencer;
    pocket::ui::UiController ui(sequencer);
    sequencer.start();
    for (int tick = 0; tick < 8; ++tick) {
        (void)sequencer.tick();
    }
    ui.dispatch({pocket::ui::Action::ScreenNext});
    ui.dispatch({pocket::ui::Action::ScreenNext});
    std::cout << "Headless shell initialized. Active screen index: "
              << static_cast<int>(ui.state().screen) << '\n';
    return 0;
}

#ifdef POCKET_HAVE_SDL2

std::optional<pocket::ui::KeyboardKey> toKeyboardKey(SDL_Keycode keycode) {
    using pocket::ui::KeyboardKey;
    switch (keycode) {
    case SDLK_UP:
        return KeyboardKey::Up;
    case SDLK_DOWN:
        return KeyboardKey::Down;
    case SDLK_LEFT:
        return KeyboardKey::Left;
    case SDLK_RIGHT:
        return KeyboardKey::Right;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return KeyboardKey::Enter;
    case SDLK_ESCAPE:
        return KeyboardKey::Escape;
    case SDLK_SPACE:
        return KeyboardKey::Space;
    case SDLK_m:
        return KeyboardKey::M;
    case SDLK_q:
        return KeyboardKey::Q;
    case SDLK_e:
        return KeyboardKey::E;
    case SDLK_PAGEUP:
        return KeyboardKey::PageUp;
    case SDLK_PAGEDOWN:
        return KeyboardKey::PageDown;
    case SDLK_TAB:
        return KeyboardKey::Tab;
    case SDLK_p:
        return KeyboardKey::P;
    case SDLK_F1:
        return KeyboardKey::F1;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        return KeyboardKey::Shift;
    default:
        return std::nullopt;
    }
}

std::optional<pocket::ui::ControllerControl> toControllerControl(
    std::uint8_t button) {
    using pocket::ui::ControllerControl;
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return ControllerControl::DpadUp;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return ControllerControl::DpadDown;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return ControllerControl::DpadLeft;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return ControllerControl::DpadRight;
    case SDL_CONTROLLER_BUTTON_A:
        return ControllerControl::A;
    case SDL_CONTROLLER_BUTTON_B:
        return ControllerControl::B;
    case SDL_CONTROLLER_BUTTON_X:
        return ControllerControl::X;
    case SDL_CONTROLLER_BUTTON_Y:
        return ControllerControl::Y;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return ControllerControl::L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return ControllerControl::R1;
    case SDL_CONTROLLER_BUTTON_BACK:
        return ControllerControl::Select;
    case SDL_CONTROLLER_BUTTON_START:
        return ControllerControl::Start;
    default:
        return std::nullopt;
    }
}

std::optional<pocket::ui::ControllerControl> genericJoystickButton(
    std::uint8_t button) {
    using pocket::ui::ControllerControl;
    switch (button) {
    case 0:
        return ControllerControl::A;
    case 1:
        return ControllerControl::B;
    case 2:
        return ControllerControl::X;
    case 3:
        return ControllerControl::Y;
    case 4:
        return ControllerControl::L1;
    case 5:
        return ControllerControl::R1;
    case 6:
        return ControllerControl::Select;
    case 7:
        return ControllerControl::Start;
    case 8:
        return ControllerControl::L2;
    case 9:
        return ControllerControl::R2;
    default:
        return std::nullopt;
    }
}

void setColor(SDL_Renderer* renderer, const pocket::ui::RgbaColor& color) {
    SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue,
                           color.alpha);
}

void fillRect(SDL_Renderer* renderer,
              std::int32_t x,
              std::int32_t y,
              std::int32_t width,
              std::int32_t height) {
    const SDL_Rect rect{x, y, width, height};
    SDL_RenderFillRect(renderer, &rect);
}

std::string formatNumber(const char* format, double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), format, value);
    return buffer;
}

std::int32_t fitScale(const std::string& text,
                      std::int32_t boxWidth,
                      std::int32_t boxHeight) {
    const auto length = std::max<std::int32_t>(1, static_cast<std::int32_t>(text.size()));
    return std::max<std::int32_t>(
        1, std::min((boxHeight * 3 / 5) / pocket::app::GlyphHeight,
                    boxWidth / (pocket::app::GlyphAdvance * length)));
}

void drawText(SDL_Renderer* renderer,
              const std::string& text,
              std::int32_t x,
              std::int32_t y,
              std::int32_t boxWidth,
              std::int32_t boxHeight,
              const pocket::ui::RgbaColor& color,
              std::int32_t forcedScale = 0) {
    if (text.empty()) {
        return;
    }
    const std::int32_t scale =
        forcedScale > 0 ? forcedScale : fitScale(text, boxWidth, boxHeight);
    const std::int32_t top =
        y + (boxHeight - pocket::app::GlyphHeight * scale) / 2;
    setColor(renderer, color);
    std::int32_t cursor = x;
    for (const char character : text) {
        const auto glyph = pocket::app::glyphFor(character);
        for (int row = 0; row < pocket::app::GlyphHeight; ++row) {
            for (int column = 0; column < pocket::app::GlyphWidth; ++column) {
                if ((glyph[row] >> (pocket::app::GlyphWidth - 1 - column)) & 1) {
                    fillRect(renderer, cursor + column * scale, top + row * scale,
                             scale, scale);
                }
            }
        }
        cursor += pocket::app::GlyphAdvance * scale;
    }
}

std::string mainTextFor(const pocket::ui::MainDrawCommand& command) {
    using pocket::ui::MainTextRole;
    switch (command.textRole) {
    case MainTextRole::Tempo:
        return formatNumber("%.0f BPM", command.numericValue);
    case MainTextRole::Transport:
        return command.playing ? "PLAY" : "STOP";
    case MainTextRole::Track: {
        std::string label = "T" + std::to_string(command.trackNumber) + " " +
                            (command.instrument == pocket::InstrumentType::Sampler
                                 ? "SMP"
                                 : "SYN");
        if (command.muted) {
            label += " M";
        }
        return label;
    }
    }
    return {};
}

void drawFooter(SDL_Renderer* renderer,
                const pocket::ui::ThemePalette& palette,
                std::int32_t width,
                std::int32_t height) {
    const std::int32_t margin = width / 24;
    const std::int32_t boxHeight = std::max(12, height / 32);
    drawText(renderer, "TAB:SCREEN Q/E:TRACK SPACE:PLAY F1:HELP P:PROJECT", margin,
             height - boxHeight - 4, width - 2 * margin, boxHeight,
             palette.subdued);
}

void renderMainScreen(SDL_Renderer* renderer,
                      const pocket::Sequencer& sequencer,
                      const pocket::ui::UiController& ui,
                      const pocket::app::DesktopAudioEngine* engine,
                      std::int32_t width,
                      std::int32_t height) {
    pocket::ui::MainScreenSnapshot snapshot;
    if (!pocket::ui::makeMainScreenSnapshot(
            sequencer, ui.state().selectedTrack, snapshot)) {
        return;
    }

    pocket::ui::MainScreenFrame frame;
    if (!pocket::ui::layoutMainScreen(snapshot, ui.systemSettings().display.theme,
                                      width, height, frame)) {
        return;
    }
    pocket::ui::MainScreenDrawList drawList;
    if (!pocket::ui::buildMainScreenDrawList(frame, drawList)) {
        return;
    }

    std::array<std::size_t, pocket::TrackCount> stepIndex{};
    for (std::size_t index = 0; index < drawList.commandCount; ++index) {
        const auto& command = drawList.commands[index];
        if (command.type == pocket::ui::MainDrawCommandType::Text) {
            drawText(renderer, mainTextFor(command), command.x, command.y,
                     command.width, command.height, command.color);
            continue;
        }
        if (command.type != pocket::ui::MainDrawCommandType::StepBar) {
            continue;
        }
        auto color = command.color;
        if (command.next) {
            color = frame.palette.accent;
        } else if (command.muted) {
            color = frame.palette.subdued;
        }
        if (engine != nullptr && command.trackNumber >= 1 &&
            command.trackNumber <= pocket::TrackCount) {
            const auto track = static_cast<std::size_t>(command.trackNumber - 1);
            const float light = engine->luminance(track, stepIndex[track]++);
            if (light > 0.0F && !command.muted) {
                const auto mix = [light](std::uint8_t from, std::uint8_t to) {
                    return static_cast<std::uint8_t>(
                        from + (static_cast<float>(to) - from) * light);
                };
                color.red = mix(color.red, frame.palette.accent.red);
                color.green = mix(color.green, frame.palette.accent.green);
                color.blue = mix(color.blue, frame.palette.accent.blue);
            }
        }
        setColor(renderer, color);
        fillRect(renderer, command.x, command.y, command.width, command.height);
    }
    drawFooter(renderer, frame.palette, width, height);
}

struct ScreenRow {
    std::string label;
    std::string value;
};

void renderRows(SDL_Renderer* renderer,
                const pocket::ui::ThemePalette& palette,
                const std::string& title,
                const std::vector<ScreenRow>& rows,
                std::size_t selectedRow,
                std::int32_t width,
                std::int32_t height) {
    const std::int32_t margin = width / 12;
    const std::int32_t pitch =
        std::max(12, height / static_cast<std::int32_t>(rows.size() + 4));
    const std::int32_t labelWidth = width / 3;
    const std::int32_t valueWidth = width / 2 - 2 * margin;
    std::int32_t scale = fitScale(title, width - 2 * margin, pitch);
    for (const auto& row : rows) {
        scale = std::min(scale, fitScale(row.label, labelWidth, pitch));
        scale = std::min(scale, fitScale(row.value, valueWidth, pitch));
    }
    drawText(renderer, title, margin, pitch / 2, width - 2 * margin, pitch,
             palette.primary, scale);
    std::int32_t y = pitch * 2;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const bool selected = row == selectedRow;
        const auto& color = selected ? palette.accent : palette.primary;
        drawText(renderer, rows[row].label, margin, y, labelWidth, pitch, color,
                 scale);
        drawText(renderer, rows[row].value, margin + width / 2, y, valueWidth,
                 pitch, color, scale);
        if (selected) {
            setColor(renderer, palette.accent);
            fillRect(renderer, margin, y + pitch - std::max(2, pitch / 12),
                     width - 2 * margin, std::max(1, pitch / 16));
        }
        y += pitch;
    }
    drawFooter(renderer, palette, width, height);
}

// MIXER: all 8 tracks with level meters, plus the master row. The selected
// track and field (from the UI state) are highlighted.
void renderMixerScreen(SDL_Renderer* renderer,
                       const pocket::ui::ThemePalette& palette,
                       const pocket::Sequencer& sequencer,
                       const pocket::ui::UiController& ui,
                       const pocket::app::DesktopAudioEngine* engine,
                       std::int32_t width,
                       std::int32_t height) {
    const std::int32_t margin = width / 12;
    const std::int32_t pitch = std::max(12, height / 13);
    const std::int32_t textHeight = pitch * 2 / 3;
    const std::size_t selected =
        std::min(ui.state().selectedTrack, pocket::TrackCount - 1);
    const auto field = static_cast<std::size_t>(ui.state().mixerField);
    const char* const fieldNames[] = {"VOLUME", "PAN", "MUTE", "MASTER"};
    const std::size_t fieldCount = sizeof(fieldNames) / sizeof(fieldNames[0]);

    drawText(renderer, "MIXER", margin, pitch / 2, width / 3, textHeight,
             palette.primary);
    const std::int32_t labelWidth = width / 12;
    const std::int32_t meterX = margin + labelWidth;
    const std::int32_t meterWidth = width / 3;
    const std::int32_t textX = meterX + meterWidth + width / 32;
    const std::int32_t textWidth = width - margin - textX;
    const std::int32_t meterHeight = std::max(4, textHeight / 2);

    const auto drawMeter = [&](std::int32_t y, float peak, bool active) {
        const float level = std::clamp(peak, 0.0F, 1.0F);
        setColor(renderer, palette.subdued);
        fillRect(renderer, meterX, y + (textHeight - meterHeight) / 2,
                 meterWidth, std::max(1, meterHeight / 4));
        setColor(renderer, active ? palette.accent : palette.primary);
        fillRect(renderer, meterX, y + (textHeight - meterHeight) / 2,
                 static_cast<std::int32_t>(level * meterWidth), meterHeight);
    };

    std::int32_t y = pitch * 2;
    for (std::size_t track = 0; track < pocket::TrackCount; ++track) {
        const auto& state = sequencer.track(track);
        const bool isSelected = track == selected;
        const auto& color = isSelected ? palette.accent : palette.primary;
        drawText(renderer, "T" + std::to_string(track + 1), margin, y,
                 labelWidth, textHeight, color);
        drawMeter(y, state.muted || engine == nullptr
                         ? 0.0F
                         : engine->trackPeak(track),
                  isSelected);
        std::string text = "VOL " + std::to_string(state.volume) + " PAN " +
                           formatNumber("%+.2f", state.pan);
        if (state.muted) {
            text += " MUTE";
        }
        drawText(renderer, text, textX, y, textWidth, textHeight, color);
        y += pitch;
    }

    const bool masterSelected = field == fieldCount - 1;
    const auto& masterColor = masterSelected ? palette.accent : palette.primary;
    drawText(renderer, "MST", margin, y, labelWidth, textHeight, masterColor);
    drawMeter(y, engine != nullptr ? engine->masterPeak() : 0.0F,
              masterSelected);
    drawText(renderer,
             std::to_string(ui.systemSettings().audio.masterVolumePercent) + "%",
             textX, y, textWidth, textHeight, masterColor);
    y += pitch;

    drawText(renderer,
             std::string("EDIT: ") + fieldNames[std::min(field, fieldCount - 1)],
             margin, y, width - 2 * margin, textHeight, palette.accent);
    drawFooter(renderer, palette, width, height);
}

std::vector<ScreenRow> editRows(const pocket::TrackState& track) {
    return {{"LENGTH", std::to_string(track.rhythm.length)},
            {"FILL", std::to_string(track.rhythm.fill)},
            {"OFFSET", std::to_string(track.rhythm.offset)},
            {"INSTRUMENT",
             track.instrument == pocket::InstrumentType::Sampler ? "SAMPLER"
                                                                 : "SYNTH"},
            {"VOLUME", std::to_string(track.volume)},
            {"MUTE", track.muted ? "ON" : "OFF"}};
}

std::vector<ScreenRow> soundRows(const pocket::TrackState& track) {
    return {{"ATTACK", formatNumber("%.0f MS", track.envelope.attackSeconds * 1000.0)},
            {"DECAY", formatNumber("%.0f MS", track.envelope.decaySeconds * 1000.0)},
            {"FILTER", track.filter.enabled ? "ON" : "OFF"},
            {"CUTOFF", formatNumber("%.0f HZ", track.filter.cutoffHz)}};
}

std::vector<ScreenRow> mixerRows(const pocket::TrackState& track,
                                 const pocket::platform::SystemSettings& settings) {
    return {{"VOLUME", std::to_string(track.volume)},
            {"PAN", formatNumber("%+.2f", track.pan)},
            {"MUTE", track.muted ? "ON" : "OFF"},
            {"MASTER", std::to_string(settings.audio.masterVolumePercent) + "%"}};
}

const char* waveformName(pocket::LfoWaveform w) {
    using pocket::LfoWaveform;
    switch (w) {
    case LfoWaveform::Sine: return "SINE";
    case LfoWaveform::Triangle: return "TRIANGLE";
    case LfoWaveform::Square: return "SQUARE";
    case LfoWaveform::SawUp: return "SAW UP";
    case LfoWaveform::SawDown: return "SAW DOWN";
    case LfoWaveform::SampleAndHold: return "S&H";
    }
    return "?";
}

const char* divisionName(pocket::LfoDivision d) {
    static const char* const names[] = {
        "1/1",  "1/1.", "1/1T", "1/2",  "1/2.", "1/2T", "1/4", "1/4.",
        "1/4T", "1/8",  "1/8.", "1/8T", "1/16", "1/16.", "1/16T"};
    const auto index = static_cast<std::size_t>(d);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "?";
}

std::vector<ScreenRow> modulationRows(const pocket::TrackState& track) {
    using namespace pocket;
    const auto& lfo = track.lfos[0];
    const auto& jitter = track.jitter;
    const char* destination = lfo.destination == ModulationDestination::Length
                                  ? "LENGTH"
                                  : (lfo.destination == ModulationDestination::Fill
                                         ? "FILL"
                                         : "OFFSET");
    const char* jitterMode = "MIX";
    switch (jitter.mode) {
    case JitterMode::Insert: jitterMode = "INSERT"; break;
    case JitterMode::Drop: jitterMode = "DROP"; break;
    case JitterMode::Shift: jitterMode = "SHIFT"; break;
    case JitterMode::Mix: break;
    }
    static const char* const rates[] = {"1/16", "1/8", "1/4", "1/2", "1/1"};
    const auto rateIndex = static_cast<std::size_t>(jitter.rate);
    const bool synced = lfo.rateMode == LfoRateMode::Synchronized;
    return {{"LFO", lfo.enabled ? "ON" : "OFF"},
            {"WAVEFORM", waveformName(lfo.waveform)},
            {"RATE MODE", synced ? "SYNC" : "FREE"},
            {"RATE", synced ? divisionName(lfo.division)
                            : formatNumber("%.2f HZ", lfo.rateHz)},
            {"DESTINATION", destination},
            {"AMOUNT", std::to_string(lfo.amount)},
            {"JITTER", jitter.enabled ? "ON" : "OFF"},
            {"INTENSITY", std::to_string(jitter.intensity)},
            {"JITTER MODE", jitterMode},
            {"JITTER RATE", rateIndex < 5 ? rates[rateIndex] : "?"},
            {"SHIFT RANGE", std::to_string(jitter.shiftRange)},
            {"CUSTOM SEED", jitter.useCustomSeed ? "ON" : "OFF"},
            {"SEED", std::to_string(jitter.randomSeed)}};
}

std::vector<ScreenRow> systemRows(const pocket::platform::SystemSettings& settings) {
    const char* theme = "QUIET DARK";
    if (settings.display.theme == pocket::platform::ThemeId::WarmPaper) {
        theme = "WARM PAPER";
    } else if (settings.display.theme == pocket::platform::ThemeId::HighContrast) {
        theme = "HIGH CONTRAST";
    }
    return {{"CLOCK", formatNumber("%.0f BPM", settings.clock.initialTempoBpm)},
            {"MIDI", settings.midi.enabled ? "ON" : "OFF"},
            {"AUDIO", std::to_string(settings.audio.sampleRate) + " HZ"},
            {"INPUT", settings.input.preferGameController ? "PAD FIRST" : "KEYS"},
            {"DISPLAY", theme},
            {"STORAGE", "DEFAULT"},
            {"ABOUT", "POCKET EUCLIDEAN"}};
}

void renderModal(SDL_Renderer* renderer,
                 const pocket::ui::ThemePalette& palette,
                 const pocket::ui::UiState& state,
                 std::int32_t width,
                 std::int32_t height) {
    using pocket::ui::Modal;
    if (state.modal == Modal::None) {
        return;
    }
    setColor(renderer, palette.background);
    fillRect(renderer, 0, 0, width, height);
    if (state.modal == Modal::Help) {
        renderRows(renderer, palette, "HELP",
                   {{"TAB", "NEXT SCREEN"},
                    {"Q / E", "PREV / NEXT TRACK"},
                    {"ARROWS", "MOVE / ADJUST"},
                    {"SHIFT", "FINE ADJUST"},
                    {"SPACE", "PLAY / STOP"},
                    {"M", "MUTE"},
                    {"P", "PROJECT MENU"},
                    {"ESC", "BACK"}},
                   99, width, height);
    } else if (state.modal == Modal::ProjectMenu) {
        renderRows(renderer, palette, "PROJECT",
                   {{"NEW", ""}, {"LOAD", ""}, {"SAVE", ""}, {"SAVE AS", ""},
                    {"RETURN", ""}},
                   static_cast<std::size_t>(state.projectMenuItem), width, height);
    } else if (state.modal == Modal::ConfirmDiscard) {
        renderRows(renderer, palette, "UNSAVED CHANGES",
                   {{"SAVE", ""}, {"DISCARD", ""}, {"CANCEL", ""}},
                   static_cast<std::size_t>(state.discardChoice), width, height);
    } else {
        renderRows(renderer, palette, "SELECT FILE", {}, 99, width, height);
    }
}

int runSdlApp(const AppOptions& options) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK |
                 SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }

    const Uint32 windowFlags =
        SDL_WINDOW_SHOWN | (options.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP
                                               : SDL_WINDOW_RESIZABLE);
    SDL_Window* window = SDL_CreateWindow("Pocket Euclidean",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, options.width,
                                          options.height, windowFlags);
    if (window == nullptr) {
        std::cerr << "Failed to create window: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    if (options.fullscreen) {
        SDL_ShowCursor(SDL_DISABLE);
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        std::cerr << "Accelerated renderer unavailable ("
                  << SDL_GetError() << "); using software rendering\n";
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer == nullptr) {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    pocket::Sequencer sequencer;
    for (std::size_t track = 0; track < pocket::TrackCount; ++track) {
        sequencer.setRhythm(track, pocket::ExamplePresetRhythms[track]);
    }
    const pocket::platform::SystemCapabilities capabilities{true,  true,  true,
                                                             true,  true,  true};
    pocket::ui::UiController ui(sequencer, capabilities);
    const std::string settingsPath =
        (std::filesystem::path(options.dataDir) / "settings.cfg").string();
    {
        pocket::platform::SystemSettings stored;
        std::string settingsError;
        if (std::filesystem::exists(settingsPath)) {
            if (pocket::platform::loadSettingsFile(settingsPath, stored,
                                                   &settingsError) &&
                ui.setSystemSettings(stored)) {
                std::cerr << "Loaded settings from " << settingsPath << '\n';
            } else {
                std::cerr << "Ignoring settings file: " << settingsError
                          << '\n';
            }
        }
    }
    std::string savedSettingsText =
        pocket::platform::serializeSettings(ui.systemSettings());
    pocket::ui::InputRepeater repeater;
    for (int i = 0; i < options.startScreen; ++i) {
        ui.dispatch({pocket::ui::Action::ScreenNext});
    }
    if (options.startHelp) {
        ui.dispatch({pocket::ui::Action::Help});
    }

    pocket::app::DesktopAudioEngine engine(sequencer);
    std::string samplesDir = options.samplesDir;
    if (samplesDir.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory("samples", ec)) {
            samplesDir = "samples";
        }
    }
    if (!samplesDir.empty()) {
        const auto count = engine.loadSampleDirectory(samplesDir, std::cerr);
        std::cerr << "Loaded " << count << " WAV sample(s) from " << samplesDir
                  << '\n';
    }
    bool audioRunning = false;
    if (options.capturePath.empty() || options.autoPlay) {
        const auto& audioSettings = ui.systemSettings().audio;
        audioRunning = engine.start(
            {options.audioRate != 0 ? options.audioRate : audioSettings.sampleRate,
             options.audioBuffer != 0 ? options.audioBuffer
                                      : audioSettings.bufferFrames});
        if (audioRunning) {
            ui.setAudioParameterQueue(&engine.parameterQueue());
            engine.parameterQueue().push(
                {pocket::audio::AudioParameter::MasterVolume, 0,
                 static_cast<float>(audioSettings.masterVolumePercent)});
            std::cerr << "Audio started: " << engine.obtainedSpec().sampleRate
                      << " Hz, " << engine.obtainedSpec().bufferFrames
                      << " frames\n";
        } else {
            std::cerr << "Audio device unavailable; running visual-only with a "
                         "wall-clock placeholder tick.\n";
        }
    }

    // The UI edits its own Sequencer; changes reach the audio thread's copy
    // through a lock-free command queue (no device lock is taken).
    std::string statusText;
    std::uint64_t statusUntilMs = 0;
    const auto setStatus = [&](const std::string& text) {
        statusText = text;
        statusUntilMs = SDL_GetTicks64() + 3000;
        std::cerr << text << '\n';
    };
    const std::string projectPath =
        (std::filesystem::path(options.dataDir) / "project.pec").string();
    const auto applyToLive = [&](const pocket::project::ProjectData& data) {
        // Validate on a scratch copy so a bad file never half-applies.
        pocket::Sequencer scratch = sequencer;
        if (!pocket::project::applyProject(data, scratch)) {
            return false;
        }
        pocket::project::applyProject(data, sequencer);
        ui.resyncFromSequencer();
        ui.setModified(false);
        if (audioRunning) {
            engine.syncSequencer(sequencer);
        }
        return true;
    };
    const auto handleProjectRequest = [&](pocket::ui::ProjectRequest request) {
        using pocket::ui::ProjectRequest;
        std::string error;
        switch (request) {
        case ProjectRequest::None:
            break;
        case ProjectRequest::NewProject: {
            pocket::project::ProjectData fresh;
            for (std::size_t i = 0; i < pocket::TrackCount; ++i) {
                fresh.tracks[i].rhythm = pocket::ExamplePresetRhythms[i];
            }
            setStatus(applyToLive(fresh) ? "NEW PROJECT" : "NEW FAILED");
            break;
        }
        case ProjectRequest::SaveProject:
        case ProjectRequest::SaveProjectAs:
            if (pocket::project::saveProjectFile(
                    projectPath, pocket::project::captureProject(sequencer),
                    &error)) {
                ui.setModified(false);
                setStatus("SAVED");
            } else {
                setStatus("SAVE FAILED: " + error);
            }
            break;
        case ProjectRequest::LoadProject: {
            pocket::project::ProjectData data;
            if (!pocket::project::loadProjectFile(projectPath, data, &error)) {
                setStatus("LOAD FAILED: " + error);
            } else {
                setStatus(applyToLive(data) ? "LOADED" : "LOAD FAILED: bad values");
            }
            break;
        }
        }
    };
    const auto dispatchUi = [&](const pocket::ui::ActionEvent& event) {
        ui.dispatch(event);
        handleProjectRequest(ui.takeProjectRequest());
        const auto settingsText =
            pocket::platform::serializeSettings(ui.systemSettings());
        if (settingsText != savedSettingsText) {
            std::string saveError;
            if (pocket::platform::saveSettingsFile(
                    settingsPath, ui.systemSettings(), &saveError)) {
                savedSettingsText = settingsText;
            } else {
                setStatus("SETTINGS SAVE FAILED: " + saveError);
                savedSettingsText = settingsText;
            }
        }
        if (audioRunning) {
            engine.syncSequencer(sequencer);
        }
    };
    if (std::filesystem::exists(projectPath)) {
        handleProjectRequest(pocket::ui::ProjectRequest::LoadProject);
    }
    if (options.autoPlay) {
        dispatchUi({pocket::ui::Action::PlayStop});
    }
    bool running = true;
    bool shiftHeld = false;
    bool leftTrigger = false;
    bool rightTrigger = false;
    std::map<SDL_JoystickID, SDL_GameController*> controllers;
    std::map<SDL_JoystickID, SDL_Joystick*> joysticks;

    bool selectHeld = false;
    bool startHeld = false;
    const auto handleControl = [&](pocket::ui::ControllerControl control,
                                   bool pressed) {
        // Handheld launchers need an in-app exit: hold Select + Start.
        if (control == pocket::ui::ControllerControl::Select) {
            selectHeld = pressed;
        } else if (control == pocket::ui::ControllerControl::Start) {
            startHeld = pressed;
        }
        if (selectHeld && startHeld) {
            running = false;
            return;
        }
        const auto mapped = pocket::ui::InputMapping::controller(control);
        if (!mapped) {
            return;
        }
        if (!pocket::ui::InputMapping::isRepeatable(mapped->action)) {
            if (pressed) {
                dispatchUi(*mapped);
            }
            return;
        }
        if (pressed) {
            const auto repeated = repeater.press(
                pocket::ui::InputSource::Controller,
                static_cast<std::uint8_t>(control), *mapped, SDL_GetTicks64());
            if (repeated) {
                dispatchUi(*repeated);
            }
        } else {
            repeater.release(pocket::ui::InputSource::Controller,
                             static_cast<std::uint8_t>(control));
        }
    };

    // Triggers are analog axes; use hysteresis to turn them into button edges.
    const auto updateTrigger = [&](bool& held,
                                   pocket::ui::ControllerControl control,
                                   std::int16_t value) {
        if (!held && value > 16000) {
            held = true;
            handleControl(control, true);
        } else if (held && value < 8000) {
            held = false;
            handleControl(control, false);
        }
    };

    const auto openDevice = [&](int deviceIndex) {
        if (SDL_IsGameController(deviceIndex)) {
            SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex);
            if (controller == nullptr) {
                std::cerr << "Failed to open controller " << deviceIndex << ": "
                          << SDL_GetError() << '\n';
                return;
            }
            const SDL_JoystickID id =
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
            if (controllers.count(id) != 0) {
                SDL_GameControllerClose(controller);
                return;
            }
            controllers[id] = controller;
            std::cerr << "Controller connected: "
                      << SDL_GameControllerName(controller) << '\n';
            return;
        }
        SDL_Joystick* joystick = SDL_JoystickOpen(deviceIndex);
        if (joystick == nullptr) {
            std::cerr << "Failed to open joystick " << deviceIndex << ": "
                      << SDL_GetError() << '\n';
            return;
        }
        const SDL_JoystickID id = SDL_JoystickInstanceID(joystick);
        if (joysticks.count(id) != 0) {
            SDL_JoystickClose(joystick);
            return;
        }
        joysticks[id] = joystick;
        char guid[33]{};
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid,
                                  sizeof(guid));
        std::cerr << "Unrecognized controller (no GameController mapping), "
                     "using generic fallback: "
                  << (SDL_JoystickName(joystick) != nullptr
                          ? SDL_JoystickName(joystick)
                          : "unknown")
                  << " GUID=" << guid << " buttons="
                  << SDL_JoystickNumButtons(joystick)
                  << " hats=" << SDL_JoystickNumHats(joystick) << '\n';
    };

    const auto closeDevice = [&](SDL_JoystickID id) {
        if (const auto it = controllers.find(id); it != controllers.end()) {
            SDL_GameControllerClose(it->second);
            controllers.erase(it);
            std::cerr << "Controller disconnected\n";
        } else if (const auto jt = joysticks.find(id); jt != joysticks.end()) {
            SDL_JoystickClose(jt->second);
            joysticks.erase(jt);
            std::cerr << "Joystick disconnected\n";
        }
    };
    const auto startTime = std::chrono::steady_clock::now();
    auto lastTick = startTime;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }

            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                const bool pressed = event.type == SDL_KEYDOWN;
                if (event.key.keysym.sym == SDLK_LSHIFT ||
                    event.key.keysym.sym == SDLK_RSHIFT) {
                    shiftHeld = pressed;
                }
                const auto key = toKeyboardKey(event.key.keysym.sym);
                if (!key) {
                    continue;
                }
                const auto mapped = pocket::ui::InputMapping::keyboard(
                    *key, shiftHeld, event.key.repeat != 0, pressed);
                if (!mapped) {
                    continue;
                }
                if (mapped->action == pocket::ui::Action::ValueFine ||
                    !pocket::ui::InputMapping::isRepeatable(mapped->action)) {
                    dispatchUi(*mapped);
                    continue;
                }
                if (pressed) {
                    const auto repeated = repeater.press(
                        pocket::ui::InputSource::Keyboard,
                        static_cast<std::uint8_t>(*key), *mapped,
                        SDL_GetTicks64());
                    if (repeated) {
                        dispatchUi(*repeated);
                    }
                } else {
                    repeater.release(pocket::ui::InputSource::Keyboard,
                                     static_cast<std::uint8_t>(*key));
                }
                continue;
            }

            if (event.type == SDL_CONTROLLERDEVICEADDED) {
                openDevice(event.cdevice.which);
                continue;
            }
            if (event.type == SDL_JOYDEVICEADDED) {
                if (!SDL_IsGameController(event.jdevice.which)) {
                    openDevice(event.jdevice.which);
                }
                continue;
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED ||
                event.type == SDL_JOYDEVICEREMOVED) {
                closeDevice(event.type == SDL_CONTROLLERDEVICEREMOVED
                                ? event.cdevice.which
                                : event.jdevice.which);
                continue;
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN ||
                event.type == SDL_CONTROLLERBUTTONUP) {
                if (const auto control = toControllerControl(
                        static_cast<std::uint8_t>(event.cbutton.button))) {
                    handleControl(*control,
                                  event.type == SDL_CONTROLLERBUTTONDOWN);
                }
                continue;
            }

            if (event.type == SDL_CONTROLLERAXISMOTION) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                    updateTrigger(leftTrigger,
                                  pocket::ui::ControllerControl::L2,
                                  event.caxis.value);
                } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    updateTrigger(rightTrigger,
                                  pocket::ui::ControllerControl::R2,
                                  event.caxis.value);
                }
                continue;
            }

            // Fallback for joysticks without a GameController mapping.
            if (event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP) {
                if (SDL_GameControllerFromInstanceID(event.jbutton.which) !=
                    nullptr) {
                    continue;
                }
                if (const auto control = genericJoystickButton(event.jbutton.button)) {
                    handleControl(*control, event.type == SDL_JOYBUTTONDOWN);
                }
                continue;
            }

            if (event.type == SDL_JOYHATMOTION) {
                if (SDL_GameControllerFromInstanceID(event.jhat.which) != nullptr) {
                    continue;
                }
                using pocket::ui::ControllerControl;
                const std::array<std::pair<std::uint8_t, ControllerControl>, 4>
                    hatMap{{{SDL_HAT_UP, ControllerControl::DpadUp},
                            {SDL_HAT_DOWN, ControllerControl::DpadDown},
                            {SDL_HAT_LEFT, ControllerControl::DpadLeft},
                            {SDL_HAT_RIGHT, ControllerControl::DpadRight}}};
                for (const auto& [mask, control] : hatMap) {
                    handleControl(control, (event.jhat.value & mask) != 0);
                }
            }
        }

        const auto repeated = repeater.poll(SDL_GetTicks64());
        if (repeated) {
            dispatchUi(*repeated);
        }

        if (!audioRunning) {
            // Visual-only fallback when no audio device is available.
            const auto now = std::chrono::steady_clock::now();
            const auto tickElapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                      lastTick);
            if (tickElapsed.count() >= 125) {
                if (sequencer.isPlaying()) {
                    sequencer.tick();
                }
                lastTick = now;
            }
        } else {
            engine.syncSequencer(sequencer);
            engine.updateLights();
            engine.updateMeters();
        }

        const pocket::ui::ThemePalette* palette =
            pocket::ui::paletteFor(ui.systemSettings().display.theme);
        if (palette == nullptr) {
            palette = pocket::ui::paletteFor(pocket::platform::ThemeId::QuietDark);
        }
        if (palette == nullptr) {
            running = false;
            continue;
        }

        setColor(renderer, palette->background);
        SDL_RenderClear(renderer);

        int width = options.width;
        int height = options.height;
        SDL_GetRendererOutputSize(renderer, &width, &height);

        {
        const auto& selectedTrack = sequencer.track(
            std::min(ui.state().selectedTrack, pocket::TrackCount - 1));
        switch (ui.state().screen) {
        case pocket::ui::Screen::Main:
            renderMainScreen(renderer, sequencer, ui,
                             audioRunning ? &engine : nullptr, width, height);
            break;
        case pocket::ui::Screen::Edit:
            renderRows(renderer, *palette, "EDIT", editRows(selectedTrack),
                       static_cast<std::size_t>(ui.state().editField), width,
                       height);
            break;
        case pocket::ui::Screen::Sound:
            renderRows(renderer, *palette, "SOUND", soundRows(selectedTrack),
                       static_cast<std::size_t>(ui.state().soundField), width,
                       height);
            break;
        case pocket::ui::Screen::Mixer:
            renderMixerScreen(renderer, *palette, sequencer, ui,
                              audioRunning ? &engine : nullptr, width, height);
            break;
        case pocket::ui::Screen::Modulation:
            renderRows(renderer, *palette,
                       "MODULATION T" +
                           std::to_string(ui.state().selectedTrack + 1),
                       modulationRows(selectedTrack),
                       static_cast<std::size_t>(ui.state().modulationField),
                       width, height);
            break;
        case pocket::ui::Screen::System:
            renderRows(renderer, *palette, "SYSTEM",
                       systemRows(ui.systemSettings()),
                       static_cast<std::size_t>(ui.state().systemCategory),
                       width, height);
            break;
        }
        renderModal(renderer, *palette, ui.state(), width, height);
        if (!statusText.empty() && SDL_GetTicks64() < statusUntilMs) {
            const auto barHeight = std::max(12, height / 22);
            drawText(renderer, statusText, height / 48, height - 2 * barHeight - height / 48,
                     width - height / 24, barHeight, palette->accent);
        }
        }

        if (!options.capturePath.empty() &&
            SDL_GetTicks64() >= static_cast<std::uint64_t>(options.captureDelayMs)) {
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
                0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
            if (surface != nullptr) {
                SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                     surface->pixels, surface->pitch);
                if (SDL_SaveBMP(surface, options.capturePath.c_str()) != 0) {
                    std::cerr << "Capture failed: " << SDL_GetError() << '\n';
                }
                SDL_FreeSurface(surface);
            }
            running = false;
        }

        SDL_RenderPresent(renderer);
    }

    for (auto& entry : controllers) {
        SDL_GameControllerClose(entry.second);
    }
    for (auto& entry : joysticks) {
        SDL_JoystickClose(entry.second);
    }
    if (audioRunning) {
        std::cerr << "Audio callback failures: " << engine.callbackFailures()
                  << ", dropped visual events: " << engine.droppedVisualEvents()
                  << '\n';
        const auto timing = engine.timingReport();
        std::cerr << "Callback load (of buffer time): chunks="
                  << timing.callbacks << " mean=" << timing.meanLoadPercent
                  << "% p99<=" << timing.p99LoadPercent
                  << "% max=" << timing.maxLoadPercent
                  << "% overBudget=" << timing.overBudget << '\n';
    }
    engine.stop();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

#endif

} // namespace

int main(int argc, char** argv) {
    AppOptions options;
    if (!parseArguments(argc, argv, options)) {
        return 2;
    }

    if (options.headless) {
        return runHeadless(options);
    }

#ifdef POCKET_HAVE_SDL2
    return runSdlApp(options);
#else
    std::cerr << "SDL2 is not available in this build. Use --headless or "
                 "configure CMake with an SDL2 package.\n";
    return runHeadless(options);
#endif
}
