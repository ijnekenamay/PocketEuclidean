#include "app/BitmapFont.h"
#ifdef POCKET_HAVE_SDL2
#include "app/DesktopAudioEngine.h"
#endif
#include "platform/SystemSettings.h"
#include "platform/SettingsStore.h"
#include "project/ProjectStore.h"
#include "sequencer/EuclideanGenerator.h"
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
#include <cmath>
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

pocket::ui::RgbaColor blendColor(const pocket::ui::RgbaColor& from,
                                 const pocket::ui::RgbaColor& to,
                                 float amount) {
    const float t = std::clamp(amount, 0.0F, 1.0F);
    return {
        static_cast<std::uint8_t>(
            from.red + (static_cast<float>(to.red) - from.red) * t),
        static_cast<std::uint8_t>(
            from.green + (static_cast<float>(to.green) - from.green) * t),
        static_cast<std::uint8_t>(
            from.blue + (static_cast<float>(to.blue) - from.blue) * t),
        255};
}

void fillRect(SDL_Renderer* renderer,
              std::int32_t x,
              std::int32_t y,
              std::int32_t width,
              std::int32_t height) {
    if (width <= 0 || height <= 0) {
        return;
    }
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

std::int32_t textPixelWidth(const std::string& text, std::int32_t scale) {
    if (text.empty() || scale <= 0) {
        return 0;
    }
    return static_cast<std::int32_t>(text.size()) * pocket::app::GlyphAdvance *
               scale -
           scale;
}

void drawText(SDL_Renderer* renderer,
              const std::string& text,
              std::int32_t x,
              std::int32_t y,
              std::int32_t boxWidth,
              std::int32_t boxHeight,
              const pocket::ui::RgbaColor& color,
              std::int32_t forcedScale = 0) {
    if (text.empty() || boxWidth <= 0 || boxHeight <= 0) {
        return;
    }
    const std::int32_t maxScale = fitScale(text, boxWidth, boxHeight);
    const std::int32_t scale =
        forcedScale > 0 ? std::min(forcedScale, maxScale) : maxScale;
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

void drawTextRight(SDL_Renderer* renderer,
                   const std::string& text,
                   std::int32_t x,
                   std::int32_t y,
                   std::int32_t boxWidth,
                   std::int32_t boxHeight,
                   const pocket::ui::RgbaColor& color,
                   std::int32_t forcedScale = 0) {
    if (text.empty() || boxWidth <= 0 || boxHeight <= 0) {
        return;
    }
    const std::int32_t maxScale = fitScale(text, boxWidth, boxHeight);
    const std::int32_t scale =
        forcedScale > 0 ? std::min(forcedScale, maxScale) : maxScale;
    const std::int32_t renderedWidth = textPixelWidth(text, scale);
    const std::int32_t startX =
        x + std::max<std::int32_t>(0, boxWidth - renderedWidth);
    drawText(renderer, text, startX, y, boxWidth, boxHeight, color, scale);
}

enum class BarMode : std::uint8_t {
    None,
    Unipolar,
    Bipolar,
    Discrete
};

struct ScreenRow {
    std::string label;
    std::string value;
    BarMode barMode = BarMode::None;
    float barRatio = 0.0F;
    bool activeState = false;
};

// Super-flat horizontal parameter bar: no borders or bevels, pure proportion.
void drawFlatBar(SDL_Renderer* renderer,
                 const pocket::ui::ThemePalette& palette,
                 std::int32_t x,
                 std::int32_t y,
                 std::int32_t width,
                 std::int32_t height,
                 BarMode mode,
                 float ratio,
                 bool selected,
                 bool activeState) {
    if (width <= 0 || height <= 0 || mode == BarMode::None) {
        return;
    }
    const auto trackColor =
        blendColor(palette.background, palette.subdued, 0.45F);
    const auto fillColor = selected ? palette.accent : palette.primary;

    if (mode == BarMode::Discrete) {
        const std::int32_t dotHeight = std::max<std::int32_t>(6, height * 2 / 5);
        const std::int32_t dotWidth = std::max<std::int32_t>(16, width / 8);
        const std::int32_t barY = y + (height - dotHeight) / 2;
        setColor(renderer, trackColor);
        fillRect(renderer, x, barY + dotHeight / 2 - 1, width, 2);
        setColor(renderer, activeState ? fillColor : palette.subdued);
        const std::int32_t pillX = activeState ? (x + dotWidth + 6) : x;
        fillRect(renderer, pillX, barY, dotWidth, dotHeight);
        return;
    }

    const std::int32_t barHeight = std::max<std::int32_t>(6, height / 3);
    const std::int32_t barY = y + (height - barHeight) / 2;
    const std::int32_t railHeight = std::max<std::int32_t>(2, barHeight / 3);
    const std::int32_t railY = y + (height - railHeight) / 2;

    setColor(renderer, trackColor);
    fillRect(renderer, x, railY, width, railHeight);

    if (mode == BarMode::Unipolar) {
        const float clamped = std::clamp(ratio, 0.0F, 1.0F);
        const std::int32_t fillWidth =
            static_cast<std::int32_t>(std::round(clamped * width));
        if (fillWidth > 0) {
            setColor(renderer, fillColor);
            fillRect(renderer, x, barY, fillWidth, barHeight);
        }
    } else if (mode == BarMode::Bipolar) {
        const float clamped = std::clamp(ratio, -1.0F, 1.0F);
        const std::int32_t centerX = x + width / 2;
        setColor(renderer, palette.subdued);
        fillRect(renderer, centerX - 1, barY - 2, 2, barHeight + 4);
        const std::int32_t span =
            static_cast<std::int32_t>(std::round(std::abs(clamped) * (width / 2)));
        setColor(renderer, fillColor);
        if (clamped > 0.001F && span > 0) {
            fillRect(renderer, centerX, barY, span, barHeight);
        } else if (clamped < -0.001F && span > 0) {
            fillRect(renderer, centerX - span, barY, span, barHeight);
        } else {
            fillRect(renderer, centerX - 2, barY, 4, barHeight);
        }
    }
}

void drawHeaderBar(SDL_Renderer* renderer,
                   const pocket::ui::ThemePalette& palette,
                   const std::string& title,
                   const std::string& contextRight,
                   std::int32_t margin,
                   std::int32_t y,
                   std::int32_t width,
                   std::int32_t headerHeight) {
    const std::int32_t titleScale =
        std::clamp(headerHeight / (pocket::app::GlyphHeight * 2), 2, 4);
    const std::int32_t rightScale = std::max(1, titleScale - 1);
    const std::int32_t usableWidth = width - 2 * margin;
    drawText(renderer, title, margin, y, usableWidth * 3 / 5, headerHeight,
             palette.primary, titleScale);
    if (!contextRight.empty()) {
        drawTextRight(renderer, contextRight, margin + usableWidth * 2 / 5, y,
                      usableWidth * 3 / 5, headerHeight, palette.subdued,
                      rightScale);
    }
    setColor(renderer, blendColor(palette.background, palette.subdued, 0.4F));
    fillRect(renderer, margin, y + headerHeight - 2, usableWidth, 1);
}

std::string headerStatusFor(const pocket::Sequencer& sequencer,
                            const pocket::ui::UiController& ui) {
    std::string status =
        formatNumber("%.0f BPM  ", sequencer.tempo()) +
        (sequencer.isPlaying() ? "PLAY  " : "STOP  ") + "T" +
        std::to_string(ui.state().selectedTrack + 1);
    if (ui.state().fineAdjust) {
        status += "  FINE";
    }
    if (ui.state().modified) {
        status += "  *";
    }
    return status;
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
                std::int32_t height,
                const std::string& hint =
                    "TAB:SCREEN  Q/E:TRACK  ARROWS:EDIT  SPACE:PLAY  M:MUTE  F1:HELP  P:PROJECT") {
    const std::int32_t margin = std::max<std::int32_t>(16, width / 16);
    const std::int32_t boxHeight = std::max<std::int32_t>(16, height / 28);
    const std::int32_t footerY = height - boxHeight - std::max<std::int32_t>(8, height / 64);
    setColor(renderer, blendColor(palette.background, palette.subdued, 0.35F));
    fillRect(renderer, margin, footerY - 4, width - 2 * margin, 1);
    drawText(renderer, hint, margin, footerY, width - 2 * margin, boxHeight,
             palette.subdued, std::clamp(height / 360, 1, 2));
}

void renderMainScreen(SDL_Renderer* renderer,
                      const pocket::Sequencer& sequencer,
                      const pocket::ui::UiController& ui,
                      const pocket::app::DesktopAudioEngine* engine,
                      std::int32_t width,
                      std::int32_t height) {
    pocket::ui::MainScreenSnapshot snapshot;
    if (!pocket::ui::makeMainScreenSnapshot(sequencer, ui.state().selectedTrack,
                                            ui.state().audioTrackSettings,
                                            snapshot)) {
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

    const std::int32_t trackTextScale =
        std::clamp(frame.rowPitch / (pocket::app::GlyphHeight * 3), 1, 3);
    const std::int32_t headerScale = std::max(2, trackTextScale + 1);
    const std::int32_t usableWidth = width - 2 * frame.tempoX;

    const auto selectedTrack = ui.effectiveTrackState(ui.state().selectedTrack);
    const std::string trackSummary =
        "TRACK " + std::to_string(ui.state().selectedTrack + 1) + "  E(" +
        std::to_string(selectedTrack.effectiveRhythm.fill) + "," +
        std::to_string(selectedTrack.effectiveRhythm.length) + "," +
        std::to_string(selectedTrack.effectiveRhythm.offset) + ")  VOL " +
        std::to_string(selectedTrack.volume) +
        (selectedTrack.muted ? "  MUTE" : "");
    drawTextRight(renderer, trackSummary, frame.patternX, frame.tempoY,
                  width - frame.tempoX - frame.patternX, frame.rowPitch,
                  frame.palette.accent, trackTextScale);
    drawTextRight(renderer,
                  ui.state().fineAdjust ? "MAIN  [FINE]" : "MAIN",
                  frame.patternX, frame.tempoY + frame.rowPitch,
                  width - frame.tempoX - frame.patternX, frame.rowPitch,
                  frame.palette.subdued, trackTextScale);
    setColor(renderer,
             blendColor(frame.palette.background, frame.palette.subdued, 0.35F));
    fillRect(renderer, frame.tempoX,
             frame.tracks[0].y - std::max<std::int32_t>(4, frame.rowPitch / 8),
             usableWidth, 1);

    std::array<std::size_t, pocket::TrackCount> stepIndex{};
    for (std::size_t index = 0; index < drawList.commandCount; ++index) {
        const auto& command = drawList.commands[index];
        if (command.type == pocket::ui::MainDrawCommandType::Text) {
            if (command.textRole == pocket::ui::MainTextRole::Tempo) {
                drawText(renderer, mainTextFor(command), command.x, command.y,
                         command.width * 2, command.height, command.color,
                         headerScale);
            } else if (command.textRole == pocket::ui::MainTextRole::Transport) {
                const auto transportColor =
                    command.playing ? frame.palette.accent : frame.palette.subdued;
                drawText(renderer, mainTextFor(command), command.x, command.y,
                         command.width * 2, command.height, transportColor,
                         trackTextScale);
            } else {
                if (command.selected) {
                    const std::int32_t markerW =
                        std::max<std::int32_t>(3, frame.tempoX / 12);
                    const std::int32_t markerH =
                        std::max<std::int32_t>(8, command.height * 3 / 5);
                    setColor(renderer, frame.palette.accent);
                    fillRect(renderer,
                             std::max<std::int32_t>(4, command.x - markerW - 6),
                             command.y + (command.height - markerH) / 2,
                             markerW, markerH);
                }
                const auto labelColor =
                    command.selected
                        ? frame.palette.accent
                        : (command.muted ? frame.palette.subdued
                                         : frame.palette.primary);
                drawText(renderer, mainTextFor(command), command.x, command.y,
                         command.width, command.height, labelColor,
                         trackTextScale);
            }
            continue;
        }
        if (command.type != pocket::ui::MainDrawCommandType::StepBar) {
            continue;
        }
        auto color = command.color;
        if (command.muted) {
            color = command.active
                        ? blendColor(frame.palette.background,
                                     frame.palette.subdued, 0.65F)
                        : blendColor(frame.palette.background,
                                     frame.palette.subdued, 0.30F);
        } else if (command.next) {
            color = frame.palette.accent;
        } else if (command.selected && command.active) {
            color = blendColor(frame.palette.primary, frame.palette.accent,
                               0.35F);
        } else if (!command.active) {
            color = blendColor(frame.palette.background, frame.palette.subdued,
                               command.selected ? 0.65F : 0.45F);
        }
        if (engine != nullptr && command.trackNumber >= 1 &&
            command.trackNumber <= pocket::TrackCount) {
            const auto track = static_cast<std::size_t>(command.trackNumber - 1);
            const float light = engine->luminance(track, stepIndex[track]++);
            if (light > 0.0F && !command.muted) {
                color = blendColor(color, frame.palette.accent, light);
            }
        }
        setColor(renderer, color);
        const std::int32_t extraH =
            (command.selected && command.active) ? 2 : 0;
        fillRect(renderer, command.x, command.y - extraH / 2, command.width,
                 command.height + extraH);
    }
    drawFooter(renderer, frame.palette, width, height);
}

void drawTrackPatternPreview(SDL_Renderer* renderer,
                             const pocket::ui::ThemePalette& palette,
                             const pocket::TrackState& track,
                             bool playing,
                             std::int32_t x,
                             std::int32_t y,
                             std::int32_t width,
                             std::int32_t height) {
    const pocket::EuclideanConfig rhythm{
        track.effectiveRhythm.length,
        track.effectiveRhythm.fill,
        track.effectiveRhythm.offset,
    };
    if (!pocket::EuclideanGenerator::isValid(rhythm) || width <= 0 ||
        height <= 0) {
        return;
    }

    const std::int32_t labelBoxWidth = std::max<std::int32_t>(110, width / 6);
    const std::int32_t scale = std::clamp(height / 18, 1, 2);
    const std::string summary = "E(" + std::to_string(rhythm.fill) + "," +
                                std::to_string(rhythm.length) + "," +
                                std::to_string(rhythm.offset) + ")";
    drawText(renderer, summary, x, y, labelBoxWidth, height, palette.accent,
             scale);

    const std::int32_t stepsX = x + labelBoxWidth + 12;
    const std::int32_t availableWidth = width - (stepsX - x);
    const std::int32_t stepPitch =
        availableWidth /
        static_cast<std::int32_t>(pocket::EuclideanGenerator::MaxLength);
    if (stepPitch <= 0) {
        return;
    }
    const std::int32_t barWidth = std::max<std::int32_t>(2, stepPitch * 3 / 5);
    const std::int32_t activeHeight = std::max<std::int32_t>(8, height * 3 / 5);
    const std::int32_t inactiveHeight =
        std::max<std::int32_t>(4, activeHeight * 3 / 5);

    for (std::uint8_t step = 0; step < rhythm.length; ++step) {
        const bool active = pocket::EuclideanGenerator::isActive(rhythm, step);
        const bool next = playing && (track.position == step);
        const std::int32_t stepX =
            stepsX + static_cast<std::int32_t>(step) * stepPitch +
            (stepPitch - barWidth) / 2;
        const std::int32_t barH = active ? activeHeight : inactiveHeight;
        const std::int32_t stepY = y + (height - barH) / 2;
        const auto color =
            next ? palette.accent
                 : (active ? palette.primary
                           : blendColor(palette.background, palette.subdued,
                                        0.55F));
        setColor(renderer, color);
        fillRect(renderer, stepX, stepY, barWidth, barH);
    }
}

void renderRows(SDL_Renderer* renderer,
                const pocket::ui::ThemePalette& palette,
                const std::string& title,
                const std::string& headerRight,
                const std::vector<ScreenRow>& rows,
                std::size_t selectedRow,
                std::int32_t width,
                std::int32_t height,
                const pocket::TrackState* patternPreviewTrack = nullptr,
                bool playing = false) {
    const std::int32_t margin = std::max<std::int32_t>(16, width / 16);
    const std::int32_t headerHeight = std::max<std::int32_t>(36, height / 12);
    const std::int32_t footerReserve = std::max<std::int32_t>(36, height / 14);
    const std::int32_t topY = std::max<std::int32_t>(12, height / 36);

    drawHeaderBar(renderer, palette, title, headerRight, margin, topY, width,
                  headerHeight);

    std::int32_t contentTop = topY + headerHeight + std::max<std::int32_t>(8, height / 64);
    if (patternPreviewTrack != nullptr) {
        const std::int32_t previewHeight = std::max<std::int32_t>(32, height / 14);
        drawTrackPatternPreview(renderer, palette, *patternPreviewTrack, playing,
                                margin, contentTop, width - 2 * margin,
                                previewHeight);
        contentTop += previewHeight + std::max<std::int32_t>(8, height / 64);
        setColor(renderer, blendColor(palette.background, palette.subdued, 0.3F));
        fillRect(renderer, margin, contentTop - 4, width - 2 * margin, 1);
    }

    const std::int32_t contentBottom = height - footerReserve;
    const std::int32_t availableHeight =
        std::max<std::int32_t>(24, contentBottom - contentTop);
    const std::size_t rowCount = std::max<std::size_t>(1, rows.size());
    const std::int32_t pitch = std::clamp<std::int32_t>(
        availableHeight / static_cast<std::int32_t>(rowCount), 20,
        std::max<std::int32_t>(28, height / 10));

    const std::int32_t usableWidth = width - 2 * margin;
    const std::int32_t labelWidth = usableWidth * 32 / 100;
    const std::int32_t barX = margin + labelWidth + usableWidth * 3 / 100;
    const std::int32_t barWidth = usableWidth * 38 / 100;
    const std::int32_t valueX = barX + barWidth + usableWidth * 3 / 100;
    const std::int32_t valueWidth = margin + usableWidth - valueX;

    const std::int32_t rowScale =
        std::clamp<std::int32_t>(pitch / (pocket::app::GlyphHeight * 2), 1, 3);

    std::int32_t y = contentTop;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        const bool selected = index == selectedRow;
        const auto& labelColor = selected ? palette.accent : palette.primary;
        const auto& valueColor = selected ? palette.accent : palette.primary;

        if (selected) {
            const std::int32_t markerW = std::max<std::int32_t>(3, width / 256);
            const std::int32_t markerH = std::max<std::int32_t>(8, pitch * 3 / 5);
            setColor(renderer, palette.accent);
            fillRect(renderer, std::max<std::int32_t>(4, margin - markerW - 6),
                     y + (pitch - markerH) / 2, markerW, markerH);
        }

        drawText(renderer, row.label, margin, y, labelWidth, pitch, labelColor,
                 rowScale);

        if (row.barMode != BarMode::None) {
            drawFlatBar(renderer, palette, barX, y, barWidth, pitch, row.barMode,
                        row.barRatio, selected, row.activeState);
            drawTextRight(renderer, row.value, valueX, y, valueWidth, pitch,
                          valueColor, rowScale);
        } else if (!row.value.empty()) {
            drawTextRight(renderer, row.value, barX, y,
                          margin + usableWidth - barX, pitch, valueColor,
                          rowScale);
        }

        y += pitch;
    }
    drawFooter(renderer, palette, width, height);
}

void renderMixerScreen(SDL_Renderer* renderer,
                       const pocket::ui::ThemePalette& palette,
                       const pocket::Sequencer& sequencer,
                       const pocket::ui::UiController& ui,
                       const pocket::app::DesktopAudioEngine* engine,
                       std::int32_t width,
                       std::int32_t height) {
    const std::int32_t margin = std::max<std::int32_t>(16, width / 16);
    const std::int32_t headerHeight = std::max<std::int32_t>(36, height / 12);
    const std::int32_t footerReserve = std::max<std::int32_t>(36, height / 14);
    const std::int32_t topY = std::max<std::int32_t>(12, height / 36);

    const std::size_t selected =
        std::min(ui.state().selectedTrack, pocket::TrackCount - 1);
    const auto field = static_cast<std::size_t>(ui.state().mixerField);
    const char* const fieldNames[] = {"VOLUME", "PAN", "MUTE", "MASTER"};
    const std::size_t fieldCount = sizeof(fieldNames) / sizeof(fieldNames[0]);

    const std::string rightContext =
        std::string("FIELD: ") + fieldNames[std::min(field, fieldCount - 1)] +
        "  (ENTER:NEXT FIELD)  " + headerStatusFor(sequencer, ui);
    drawHeaderBar(renderer, palette, "MIXER", rightContext, margin, topY, width,
                  headerHeight);

    const std::int32_t contentTop =
        topY + headerHeight + std::max<std::int32_t>(8, height / 64);
    const std::int32_t availableHeight =
        std::max<std::int32_t>(120, height - footerReserve - contentTop);
    const std::int32_t pitch = availableHeight / 10;
    const std::int32_t rowScale =
        std::clamp<std::int32_t>(pitch / (pocket::app::GlyphHeight * 2), 1, 2);

    const std::int32_t usableWidth = width - 2 * margin;
    const std::int32_t labelWidth = usableWidth * 10 / 100;
    const std::int32_t volBarX = margin + labelWidth;
    const std::int32_t volBarW = usableWidth * 32 / 100;
    const std::int32_t panBarX = volBarX + volBarW + usableWidth * 3 / 100;
    const std::int32_t panBarW = usableWidth * 18 / 100;
    const std::int32_t textX = panBarX + panBarW + usableWidth * 3 / 100;
    const std::int32_t textWidth = margin + usableWidth - textX;

    const bool masterSelected = field == fieldCount - 1;

    std::int32_t y = contentTop;
    for (std::size_t track = 0; track < pocket::TrackCount; ++track) {
        const auto state = ui.effectiveTrackState(track);
        const bool isSelected = (track == selected) && !masterSelected;
        const auto& color =
            isSelected ? palette.accent
                       : (state.muted ? palette.subdued : palette.primary);

        if (isSelected) {
            const std::int32_t markerW = std::max<std::int32_t>(3, width / 256);
            const std::int32_t markerH = std::max<std::int32_t>(8, pitch * 3 / 5);
            setColor(renderer, palette.accent);
            fillRect(renderer, std::max<std::int32_t>(4, margin - markerW - 6),
                     y + (pitch - markerH) / 2, markerW, markerH);
        }

        drawText(renderer, "T" + std::to_string(track + 1), margin, y,
                 labelWidth, pitch, color, rowScale);

        const float volRatio =
            state.muted ? 0.0F : static_cast<float>(state.volume) / 100.0F;
        drawFlatBar(renderer, palette, volBarX, y, volBarW, pitch,
                    BarMode::Unipolar, volRatio,
                    isSelected && field == 0, !state.muted);
        if (!state.muted && engine != nullptr) {
            const float peak = std::clamp(engine->trackPeak(track), 0.0F, 1.0F);
            if (peak > 0.0F) {
                setColor(renderer, palette.accent);
                fillRect(renderer, volBarX, y + pitch * 3 / 4,
                         static_cast<std::int32_t>(peak * volBarW), 2);
            }
        }

        drawFlatBar(renderer, palette, panBarX, y, panBarW, pitch,
                    BarMode::Bipolar, state.pan,
                    isSelected && field == 1, !state.muted);

        std::string text = "VOL " + std::to_string(state.volume) + "  PAN " +
                           formatNumber("%+.2f", state.pan);
        if (state.muted) {
            text += "  MUTE";
        }
        drawTextRight(renderer, text, textX, y, textWidth, pitch, color,
                      rowScale);
        y += pitch;
    }

    setColor(renderer, blendColor(palette.background, palette.subdued, 0.35F));
    fillRect(renderer, margin, y + 2, usableWidth, 1);
    y += std::max<std::int32_t>(6, pitch / 5);

    const auto& masterColor = masterSelected ? palette.accent : palette.primary;
    if (masterSelected) {
        const std::int32_t markerW = std::max<std::int32_t>(3, width / 256);
        const std::int32_t markerH = std::max<std::int32_t>(8, pitch * 3 / 5);
        setColor(renderer, palette.accent);
        fillRect(renderer, std::max<std::int32_t>(4, margin - markerW - 6),
                 y + (pitch - markerH) / 2, markerW, markerH);
    }
    drawText(renderer, "MST", margin, y, labelWidth, pitch, masterColor,
             rowScale);
    const float masterRatio =
        static_cast<float>(ui.systemSettings().audio.masterVolumePercent) /
        100.0F;
    drawFlatBar(renderer, palette, volBarX, y, volBarW + panBarW, pitch,
                BarMode::Unipolar, masterRatio, masterSelected, true);
    if (engine != nullptr) {
        const float masterPeak = std::clamp(engine->masterPeak(), 0.0F, 1.0F);
        if (masterPeak > 0.0F) {
            setColor(renderer, palette.accent);
            fillRect(renderer, volBarX, y + pitch * 3 / 4,
                     static_cast<std::int32_t>(masterPeak * (volBarW + panBarW)),
                     2);
        }
    }
    drawTextRight(
        renderer,
        "MASTER " +
            std::to_string(ui.systemSettings().audio.masterVolumePercent) + "%",
        textX, y, textWidth, pitch, masterColor, rowScale);

    drawFooter(renderer, palette, width, height);
}

std::vector<ScreenRow> editRows(const pocket::TrackState& track) {
    const float lengthRatio =
        static_cast<float>(track.rhythm.length) /
        static_cast<float>(pocket::EuclideanGenerator::MaxLength);
    const float fillRatio =
        track.rhythm.length > 0
            ? static_cast<float>(track.rhythm.fill) /
                  static_cast<float>(track.rhythm.length)
            : 0.0F;
    const float offsetRatio =
        static_cast<float>(track.rhythm.offset) /
        static_cast<float>(pocket::EuclideanGenerator::MaxLength - 1);
    const float volumeRatio = static_cast<float>(track.volume) / 100.0F;

    return {{"ELENG", std::to_string(track.rhythm.length), BarMode::Unipolar,
             lengthRatio, true},
            {"EFILL", std::to_string(track.rhythm.fill), BarMode::Unipolar,
             fillRatio, true},
            {"EOFFS", std::to_string(track.rhythm.offset), BarMode::Unipolar,
             offsetRatio, true},
            {"INSTRUMENT",
             track.instrument == pocket::InstrumentType::Sampler ? "SAMPLER"
                                                                 : "SYNTH",
             BarMode::Discrete, 0.0F,
             track.instrument == pocket::InstrumentType::Synth},
            {"VOLUME", std::to_string(track.volume) + "%", BarMode::Unipolar,
             volumeRatio, true},
            {"MUTE", track.muted ? "ON" : "OFF", BarMode::Discrete, 0.0F,
             track.muted}};
}

std::vector<ScreenRow> soundRows(const pocket::TrackState& track) {
    const float attackRatio =
        (track.envelope.attackSeconds -
         pocket::AdEnvelopeSettings::MinAttackSeconds) /
        (pocket::AdEnvelopeSettings::MaxAttackSeconds -
         pocket::AdEnvelopeSettings::MinAttackSeconds);
    const float decayRatio =
        (track.envelope.decaySeconds -
         pocket::AdEnvelopeSettings::MinDecaySeconds) /
        (pocket::AdEnvelopeSettings::MaxDecaySeconds -
         pocket::AdEnvelopeSettings::MinDecaySeconds);
    const float cutoffRatio =
        (track.filter.cutoffHz - pocket::TrackFilterSettings::MinCutoffHz) /
        (pocket::TrackFilterSettings::MaxCutoffHz -
         pocket::TrackFilterSettings::MinCutoffHz);

    return {{"ATTACK",
             formatNumber("%.0f MS", track.envelope.attackSeconds * 1000.0),
             BarMode::Unipolar, attackRatio, true},
            {"DECAY",
             formatNumber("%.0f MS", track.envelope.decaySeconds * 1000.0),
             BarMode::Unipolar, decayRatio, true},
            {"FILTER", track.filter.enabled ? "ON" : "OFF", BarMode::Discrete,
             0.0F, track.filter.enabled},
            {"CUTOFF", formatNumber("%.0f HZ", track.filter.cutoffHz),
             BarMode::Unipolar, cutoffRatio, track.filter.enabled}};
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
                                  ? "ELENG"
                                  : (lfo.destination == ModulationDestination::Fill
                                         ? "EFILL"
                                         : "EOFFS");
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
    const float rateRatio =
        synced ? static_cast<float>(static_cast<int>(lfo.division) + 1) /
                     static_cast<float>(static_cast<int>(LfoDivision::Count))
               : static_cast<float>(
                     (lfo.rateHz - ModulationEngine::MinFreeRateHz) /
                     (ModulationEngine::MaxFreeRateHz -
                      ModulationEngine::MinFreeRateHz));
    const float amountRatio = static_cast<float>(lfo.amount) / 31.0F;
    const float jitterIntensityRatio =
        static_cast<float>(jitter.intensity) / 100.0F;
    const float shiftRangeRatio =
        static_cast<float>(jitter.shiftRange) /
        static_cast<float>(MaxJitterShiftTicks);

    return {{"LFO", lfo.enabled ? "ON" : "OFF", BarMode::Discrete, 0.0F,
             lfo.enabled},
            {"WAVEFORM", waveformName(lfo.waveform), BarMode::Unipolar,
             static_cast<float>(static_cast<int>(lfo.waveform) + 1) / 6.0F,
             true},
            {"RATE MODE", synced ? "SYNC" : "FREE", BarMode::Discrete, 0.0F,
             synced},
            {"RATE",
             synced ? divisionName(lfo.division)
                    : formatNumber("%.2f HZ", lfo.rateHz),
             BarMode::Unipolar, rateRatio, true},
            {"TARGET", destination, BarMode::Unipolar,
             static_cast<float>(static_cast<int>(lfo.destination) + 1) / 3.0F,
             true},
            {"DEPTH", formatNumber("%+d", static_cast<int>(lfo.amount)),
             BarMode::Bipolar, amountRatio, true},
            {"JITTER", jitter.enabled ? "ON" : "OFF", BarMode::Discrete, 0.0F,
             jitter.enabled},
            {"STRENGTH", std::to_string(jitter.intensity) + "%",
             BarMode::Unipolar, jitterIntensityRatio, true},
            {"JITTER MODE", jitterMode, BarMode::Unipolar,
             static_cast<float>(static_cast<int>(jitter.mode) + 1) / 4.0F,
             true},
            {"JITTER RATE", rateIndex < 5 ? rates[rateIndex] : "?",
             BarMode::Unipolar, static_cast<float>(rateIndex + 1) / 5.0F, true},
            {"MAX SHIFT", std::to_string(jitter.shiftRange), BarMode::Unipolar,
             shiftRangeRatio, true},
            {"CUSTOM SEED", jitter.useCustomSeed ? "ON" : "OFF",
             BarMode::Discrete, 0.0F, jitter.useCustomSeed},
            {"SEED", std::to_string(jitter.randomSeed), BarMode::None, 0.0F,
             false}};
}

std::vector<ScreenRow> systemRows(const pocket::platform::SystemSettings& settings) {
    const char* theme = "QUIET DARK";
    float themeRatio = 1.0F / 3.0F;
    if (settings.display.theme == pocket::platform::ThemeId::WarmPaper) {
        theme = "WARM PAPER";
        themeRatio = 2.0F / 3.0F;
    } else if (settings.display.theme == pocket::platform::ThemeId::HighContrast) {
        theme = "HIGH CONTRAST";
        themeRatio = 1.0F;
    }
    const float tempoRatio =
        static_cast<float>((settings.clock.initialTempoBpm -
                            pocket::Sequencer::MinTempoBpm) /
                           (pocket::Sequencer::MaxTempoBpm -
                            pocket::Sequencer::MinTempoBpm));
    return {{"CLOCK", formatNumber("%.0f BPM", settings.clock.initialTempoBpm),
             BarMode::Unipolar, tempoRatio, true},
            {"MIDI", settings.midi.enabled ? "ON" : "OFF", BarMode::Discrete,
             0.0F, settings.midi.enabled},
            {"AUDIO", std::to_string(settings.audio.sampleRate) + " HZ",
             BarMode::None, 0.0F, false},
            {"INPUT", settings.input.preferGameController ? "PAD FIRST" : "KEYS",
             BarMode::Discrete, 0.0F, settings.input.preferGameController},
            {"DISPLAY", theme, BarMode::Unipolar, themeRatio, true},
            {"STORAGE", "DEFAULT", BarMode::None, 0.0F, false},
            {"ABOUT", "POCKET EUCLIDEAN", BarMode::None, 0.0F, false}};
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
        renderRows(renderer, palette, "HELP", "ESC:CLOSE",
                   {{"TAB / L1 R1", "SWITCH SCREEN"},
                    {"Q / E / L2 R2", "PREV / NEXT TRACK"},
                    {"ARROWS / DPAD", "MOVE / ADJUST VALUE"},
                    {"SHIFT / X", "FINE ADJUST"},
                    {"SPACE / START", "PLAY / STOP"},
                    {"M / Y", "MUTE TRACK"},
                    {"P / SELECT", "PROJECT MENU"},
                    {"ESC / B", "BACK"}},
                   99, width, height);
    } else if (state.modal == Modal::ProjectMenu) {
        renderRows(renderer, palette, "PROJECT", "ENTER:SELECT  ESC:BACK",
                   {{"NEW PROJECT", ""},
                    {"LOAD PROJECT", ""},
                    {"SAVE PROJECT", ""},
                    {"SAVE PROJECT AS", ""},
                    {"RETURN", ""}},
                   static_cast<std::size_t>(state.projectMenuItem), width,
                   height);
    } else if (state.modal == Modal::ConfirmDiscard) {
        renderRows(renderer, palette, "UNSAVED CHANGES", "ENTER:CONFIRM",
                   {{"SAVE", ""}, {"DISCARD", ""}, {"CANCEL", ""}},
                   static_cast<std::size_t>(state.discardChoice), width,
                   height);
    } else {
        renderRows(renderer, palette, "SELECT FILE", "ESC:BACK", {}, 99, width,
                   height);
    }
}

void pushAllTrackAudioParameters(
    const pocket::ui::UiController& ui,
    pocket::audio::AudioParameterQueue& queue) noexcept {
    using pocket::audio::AudioParameter;
    for (std::size_t track = 0; track < pocket::TrackCount; ++track) {
        const auto& settings = ui.state().audioTrackSettings[track];
        const auto index = static_cast<std::uint8_t>(track);
        queue.push({AudioParameter::TrackVolume, index,
                    static_cast<float>(settings.volume)});
        queue.push({AudioParameter::TrackPan, index, settings.pan});
        queue.push(
            {AudioParameter::TrackMute, index, settings.muted ? 1.0F : 0.0F});
        queue.push({AudioParameter::TrackAttack, index,
                    settings.envelope.attackSeconds});
        queue.push({AudioParameter::TrackDecay, index,
                    settings.envelope.decaySeconds});
        queue.push({AudioParameter::TrackFilterEnabled, index,
                    settings.filter.enabled ? 1.0F : 0.0F});
        queue.push({AudioParameter::TrackFilterCutoff, index,
                    settings.filter.cutoffHz});
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
            pushAllTrackAudioParameters(ui, engine.parameterQueue());
            std::cerr << "Audio started: " << engine.obtainedSpec().sampleRate
                      << " Hz, " << engine.obtainedSpec().bufferFrames
                      << " frames\n";
        } else {
            std::cerr << "Audio device unavailable; running visual-only with a "
                         "wall-clock placeholder tick.\n";
        }
    }

    std::string statusText;
    std::uint64_t statusUntilMs = 0;
    const auto setStatus = [&](const std::string& text) {
        statusText = text;
        statusUntilMs = SDL_GetTicks64() + 3000;
        std::cerr << text << '\n';
    };

    pocket::project::ProjectEffectState currentEffects{};
    const std::string projectPath =
        (std::filesystem::path(options.dataDir) / "project.pec").string();
    const auto applyToLive = [&](const pocket::project::ProjectData& data) {
        pocket::Sequencer scratch = sequencer;
        if (!pocket::project::applyProject(data, scratch)) {
            return false;
        }
        pocket::project::applyProject(data, sequencer);
        ui.resyncFromSequencer();
        ui.setModified(false);
        currentEffects = data.effects;
        if (audioRunning) {
            engine.syncSequencer(sequencer);
            engine.setEffectState(currentEffects);
            pushAllTrackAudioParameters(ui, engine.parameterQueue());
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
            currentEffects = fresh.effects;
            setStatus(applyToLive(fresh) ? "NEW PROJECT" : "NEW FAILED");
            break;
        }
        case ProjectRequest::SaveProject:
        case ProjectRequest::SaveProjectAs:
            if (pocket::project::saveProjectFile(
                    projectPath,
                    pocket::project::captureProject(
                        sequencer, ui.state().audioTrackSettings, &currentEffects),
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
                currentEffects = data.effects;
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
            engine.setEffectState(currentEffects);
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
            engine.setEffectState(currentEffects);
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
            const std::size_t trackIndex =
                std::min(ui.state().selectedTrack, pocket::TrackCount - 1);
            const auto selectedTrack = ui.effectiveTrackState(trackIndex);
            const std::string trackTitleSuffix =
                " T" + std::to_string(trackIndex + 1);
            const std::string headerStatus = headerStatusFor(sequencer, ui);

            switch (ui.state().screen) {
            case pocket::ui::Screen::Main:
                renderMainScreen(renderer, sequencer, ui,
                                 audioRunning ? &engine : nullptr, width,
                                 height);
                break;
            case pocket::ui::Screen::Edit:
                renderRows(renderer, *palette, "EDIT" + trackTitleSuffix,
                           headerStatus, editRows(selectedTrack),
                           static_cast<std::size_t>(ui.state().editField),
                           width, height, &selectedTrack,
                           sequencer.isPlaying());
                break;
            case pocket::ui::Screen::Sound:
                renderRows(renderer, *palette, "SOUND" + trackTitleSuffix,
                           headerStatus, soundRows(selectedTrack),
                           static_cast<std::size_t>(ui.state().soundField),
                           width, height);
                break;
            case pocket::ui::Screen::Mixer:
                renderMixerScreen(renderer, *palette, sequencer, ui,
                                  audioRunning ? &engine : nullptr, width,
                                  height);
                break;
            case pocket::ui::Screen::Modulation:
                renderRows(renderer, *palette, "MODULATION" + trackTitleSuffix,
                           headerStatus, modulationRows(selectedTrack),
                           static_cast<std::size_t>(ui.state().modulationField),
                           width, height);
                break;
            case pocket::ui::Screen::System:
                renderRows(renderer, *palette, "SYSTEM", headerStatus,
                           systemRows(ui.systemSettings()),
                           static_cast<std::size_t>(ui.state().systemCategory),
                           width, height);
                break;
            }
            renderModal(renderer, *palette, ui.state(), width, height);
            if (!statusText.empty() && SDL_GetTicks64() < statusUntilMs) {
                const auto barHeight = std::max(12, height / 26);
                drawText(renderer, statusText, height / 48,
                         height - 2 * barHeight - height / 48,
                         width - height / 24, barHeight, palette->accent, 2);
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
