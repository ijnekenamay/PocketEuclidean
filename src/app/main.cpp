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
#include <cstddef>
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
#include <utility>
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
    std::string injectKeys;
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
        if (arg == "--inject-keys" && index + 1 < argc) {
            options.injectKeys = argv[++index];
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
                   "when combined with --play)\n"
                << "  --inject-keys <spec> Self-test: push synthetic key events\n"
                << "                    (e.g. \"space:tap,space:tap\"; keys:\n"
                << "                    up/down/left/right/enter/escape/space/\n"
                << "                    m/q/e/tab/pageup/pagedown/p/f1/shift;\n"
                << "                    actions: tap/down/up/repeat)\n";
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
        blendColor(palette.background, palette.subdued, selected ? 0.50F : 0.35F);
    const auto fillColor = selected ? palette.accent : palette.primary;

    if (mode == BarMode::Discrete) {
        const std::int32_t dotHeight = std::max<std::int32_t>(8, height * 9 / 20);
        const std::int32_t dotWidth = std::max<std::int32_t>(28, width * 22 / 100);
        const std::int32_t barY = y + (height - dotHeight) / 2;
        const std::int32_t railH = std::max<std::int32_t>(3, dotHeight / 3);
        setColor(renderer, trackColor);
        fillRect(renderer, x, y + (height - railH) / 2, width, railH);

        // Two-segment pill indicator (OFF left, ON right)
        const std::int32_t offX = x;
        const std::int32_t onX = x + dotWidth + 8;
        setColor(renderer, !activeState ? (selected ? palette.accent : palette.primary)
                                        : blendColor(palette.background, palette.subdued, 0.30F));
        fillRect(renderer, offX, barY, dotWidth, dotHeight);
        setColor(renderer, activeState ? fillColor
                                       : blendColor(palette.background, palette.subdued, 0.30F));
        fillRect(renderer, onX, barY, dotWidth, dotHeight);
        return;
    }

    const std::int32_t barHeight =
        std::max<std::int32_t>(8, selected ? (height * 11 / 20) : (height * 9 / 20));
    const std::int32_t barY = y + (height - barHeight) / 2;

    // Full-height subtle track bed + fine tick marks at 25%, 50%, 75%
    setColor(renderer, trackColor);
    fillRect(renderer, x, barY, width, barHeight);
    const auto tickColor = blendColor(palette.background, palette.subdued, 0.65F);
    setColor(renderer, tickColor);
    for (int q = 1; q <= 3; ++q) {
        const std::int32_t tx = x + (width * q) / 4;
        fillRect(renderer, tx, barY, 1, barHeight);
    }

    if (mode == BarMode::Unipolar) {
        const float clamped = std::clamp(ratio, 0.0F, 1.0F);
        const std::int32_t fillWidth =
            static_cast<std::int32_t>(std::round(clamped * width));
        if (fillWidth > 0) {
            setColor(renderer, activeState ? fillColor : palette.subdued);
            fillRect(renderer, x, barY, fillWidth, barHeight);
            if (selected && fillWidth >= 3) {
                setColor(renderer, palette.primary);
                fillRect(renderer, x + fillWidth - 2, barY - 1, 2, barHeight + 2);
            }
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

// Abstract geometric instrument & track status icon:
// - Sampler: 5 symmetrical acoustic spectrum / wave bars (35%, 70%, 100%, 70%, 35%)
// - Synth: Concentric geometric oscillator frame with a floating inner core
// - Selected: Luminous left vertical accent bar + accent-tinted glyph
// - Muted: Dimmed hollow/split motif with a horizontal mute cut
void drawInstrumentGlyph(SDL_Renderer* renderer,
                         const pocket::ui::ThemePalette& palette,
                         pocket::InstrumentType instrument,
                         bool selected,
                         bool muted,
                         std::int32_t x,
                         std::int32_t y,
                         std::int32_t width,
                         std::int32_t height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const std::int32_t iconSize =
        std::clamp<std::int32_t>(std::min(width - 6, height - 10), 10, 30);
    const std::int32_t ix = x + (width - iconSize) / 2;
    const std::int32_t iy = y + (height - iconSize) / 2;

    if (selected) {
        setColor(renderer, palette.accent);
        const std::int32_t barH = std::max<std::int32_t>(10, height - 8);
        fillRect(renderer, std::max<std::int32_t>(4, x - 6),
                 y + (height - barH) / 2, 3, barH);
    }

    const auto glyphColor =
        muted ? blendColor(palette.background, palette.subdued, 0.55F)
              : (selected ? palette.accent : palette.primary);

    if (instrument == pocket::InstrumentType::Sampler) {
        // 5 symmetrical vertical wave bars (diamond profile)
        constexpr int BarCount = 5;
        constexpr int Ratios[BarCount] = {36, 68, 100, 68, 36};
        const std::int32_t gap = std::max<std::int32_t>(1, iconSize / 10);
        const std::int32_t barW =
            std::max<std::int32_t>(2, (iconSize - gap * (BarCount - 1)) / BarCount);
        const std::int32_t totalW = barW * BarCount + gap * (BarCount - 1);
        const std::int32_t startX = x + (width - totalW) / 2;
        setColor(renderer, glyphColor);
        for (int b = 0; b < BarCount; ++b) {
            const std::int32_t bh =
                std::max<std::int32_t>(2, (iconSize * Ratios[b]) / 100);
            const std::int32_t bx = startX + b * (barW + gap);
            const std::int32_t by = y + (height - bh) / 2;
            fillRect(renderer, bx, by, barW, bh);
        }
    } else {
        // Synth: Concentric geometric oscillator square + inner resonant core
        const std::int32_t stroke = std::max<std::int32_t>(2, iconSize / 7);
        setColor(renderer, glyphColor);
        // Outer square frame
        fillRect(renderer, ix, iy, iconSize, stroke);
        fillRect(renderer, ix, iy + iconSize - stroke, iconSize, stroke);
        fillRect(renderer, ix, iy, stroke, iconSize);
        fillRect(renderer, ix + iconSize - stroke, iy, stroke, iconSize);
        // Inner solid core
        const std::int32_t coreSize =
            std::max<std::int32_t>(4, iconSize - stroke * 4);
        const std::int32_t cx = ix + (iconSize - coreSize) / 2;
        const std::int32_t cy = iy + (iconSize - coreSize) / 2;
        if (!muted) {
            setColor(renderer, selected ? palette.primary : palette.accent);
        }
        fillRect(renderer, cx, cy, coreSize, coreSize);
    }

    if (muted) {
        // Crisp horizontal mute cut across center of glyph
        setColor(renderer, palette.background);
        fillRect(renderer, ix - 2, y + height / 2 - 2, iconSize + 4, 4);
        setColor(renderer, palette.subdued);
        fillRect(renderer, ix - 1, y + height / 2 - 1, iconSize + 2, 2);
    }
}

void drawHeaderBar(SDL_Renderer* renderer,
                   const pocket::ui::ThemePalette& palette,
                   const std::string& title,
                   const std::string& contextRight,
                   std::int32_t margin,
                   std::int32_t y,
                   std::int32_t width,
                   std::int32_t headerHeight,
                   std::optional<pocket::InstrumentType> headerInstrument = std::nullopt,
                   bool headerMuted = false) {
    const std::int32_t titleScale =
        std::clamp(headerHeight / (pocket::app::GlyphHeight * 2), 2, 4);
    const std::int32_t rightScale = std::max(1, titleScale - 1);
    const std::int32_t usableWidth = width - 2 * margin;

    std::int32_t textX = margin + 14;
    if (headerInstrument.has_value()) {
        const std::int32_t glyphW = std::max<std::int32_t>(26, headerHeight - 8);
        drawInstrumentGlyph(renderer, palette, *headerInstrument, true,
                            headerMuted, margin + 4, y + 2, glyphW,
                            headerHeight - 6);
        textX = margin + glyphW + 14;
    } else {
        setColor(renderer, palette.accent);
        fillRect(renderer, margin, y + 4, 6, headerHeight - 10);
    }

    drawText(renderer, title, textX, y, usableWidth * 3 / 5, headerHeight,
             palette.primary, titleScale);
    if (!contextRight.empty()) {
        drawTextRight(renderer, contextRight, margin + usableWidth * 2 / 5, y,
                      usableWidth * 3 / 5, headerHeight, palette.subdued,
                      rightScale);
    }
    setColor(renderer, blendColor(palette.background, palette.subdued, 0.5F));
    fillRect(renderer, margin, y + headerHeight - 2, usableWidth, 2);
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
    case MainTextRole::Track:
        return {};
    }
    return {};
}

void drawFooter(SDL_Renderer* renderer,
                const pocket::ui::ThemePalette& palette,
                std::int32_t width,
                std::int32_t height,
                const std::string& hint =
                    "TAB:SCREEN  Q/E:TRACK  ARROWS:EDIT  SPACE:PLAY  M:MUTE  F1:HELP  P:PROJECT") {
    const std::int32_t margin = std::max<std::int32_t>(16, width / 24);
    const std::int32_t boxHeight = std::max<std::int32_t>(16, height / 28);
    const std::int32_t footerY = height - boxHeight - std::max<std::int32_t>(6, height / 72);
    setColor(renderer, blendColor(palette.background, palette.subdued, 0.45F));
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
    const bool livePlaying = engine != nullptr && sequencer.isPlaying();
    pocket::ui::MainScreenLiveModulation live;
    if (livePlaying) {
        live.mirror = &engine->modulation();
        live.active = true;
    }
    if (!pocket::ui::makeMainScreenSnapshot(
            sequencer, ui.state().selectedTrack,
            ui.state().audioTrackSettings, live, snapshot)) {
        return;
    }
    if (engine != nullptr) {
        for (std::size_t t = 0; t < pocket::TrackCount; ++t) {
            const auto ph = engine->playhead().read(t);
            const auto len = snapshot.tracks[t].rhythm.length;
            if (ph.length != 0) {
                snapshot.tracks[t].position =
                    static_cast<std::uint8_t>(ph.position % len);
            }
        }
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

    const std::int32_t headerAreaH = frame.tracks[0].y - frame.tempoY;
    const std::int32_t trackTextScale =
        std::clamp(frame.rowPitch / (pocket::app::GlyphHeight * 4), 1, 2);
    const std::int32_t headerScale = std::clamp(headerAreaH / 18, 2, 3);
    const std::int32_t usableWidth = width - 2 * frame.tempoX;
    const std::int32_t patternW = width - frame.tempoX - frame.patternX;

    // Header band: Selected track summary + geometric 8-track indicator pips
    const auto selectedTrack = ui.effectiveTrackState(ui.state().selectedTrack);
    pocket::EffectiveRhythm shownRhythm = selectedTrack.effectiveRhythm;
    bool modulated = false;
    bool lfoLive = false;
    float lfoValue = 0.0F;
    if (livePlaying) {
        const auto published =
            engine->modulation().read(ui.state().selectedTrack);
        if (published.valid) {
            shownRhythm = published.rhythm;
            lfoLive = true;
            lfoValue = published.lfoValue;
            modulated =
                shownRhythm.length != selectedTrack.rhythm.length ||
                shownRhythm.fill != selectedTrack.rhythm.fill ||
                shownRhythm.offset != selectedTrack.rhythm.offset;
        }
    }
    const std::string trackSummary =
        std::string(modulated ? "MOD " : "") + "E(" +
        std::to_string(shownRhythm.fill) + "," +
        std::to_string(shownRhythm.length) + "," +
        std::to_string(shownRhythm.offset) + ")  VOL " +
        std::to_string(selectedTrack.volume) + "%" +
        (selectedTrack.muted ? "  [MUTE]" : "");
    if (lfoLive && selectedTrack.lfos[0].enabled) {
        // Live modulation tick: brightness follows the LFO output so the
        // header visibly breathes with the current modulation.
        const std::int32_t summaryScale = std::min<std::int32_t>(
            trackTextScale,
            fitScale(trackSummary, patternW, headerAreaH - 12));
        const std::int32_t summaryW =
            textPixelWidth(trackSummary, summaryScale);
        const std::int32_t ledSize = 6;
        const std::int32_t ledX =
            frame.patternX + std::max<std::int32_t>(0, patternW - summaryW) -
            ledSize - 6;
        const float intensity =
            0.20F + 0.80F * std::min(1.0F, std::abs(lfoValue));
        setColor(renderer, blendColor(frame.palette.background,
                                      frame.palette.accent, intensity));
        fillRect(renderer, ledX,
                 frame.tempoY + (headerAreaH - ledSize) / 2, ledSize, ledSize);
    }
    drawTextRight(renderer, trackSummary, frame.patternX, frame.tempoY + 4,
                  patternW, headerAreaH - 12, frame.palette.accent,
                  trackTextScale);

    // 8-track geometric selector pips in the top center
    const std::int32_t pipSize = 8;
    const std::int32_t pipGap = 5;
    const std::int32_t pipsTotalW =
        static_cast<std::int32_t>(pocket::TrackCount) * pipSize +
        static_cast<std::int32_t>(pocket::TrackCount - 1) * pipGap;
    const std::int32_t pipsX = frame.tempoX + (usableWidth - pipsTotalW) / 2;
    const std::int32_t pipsY = frame.tempoY + (headerAreaH - pipSize - 6) / 2;
    for (std::size_t t = 0; t < pocket::TrackCount; ++t) {
        const bool isSel = (t == ui.state().selectedTrack);
        const bool isMuted = snapshot.tracks[t].muted;
        setColor(renderer,
                 isSel ? frame.palette.accent
                       : (isMuted ? blendColor(frame.palette.background,
                                               frame.palette.subdued, 0.35F)
                                  : frame.palette.subdued));
        fillRect(renderer,
                 pipsX + static_cast<std::int32_t>(t) * (pipSize + pipGap),
                 isSel ? (pipsY - 2) : pipsY, pipSize,
                 isSel ? (pipSize + 4) : pipSize);
    }

    setColor(renderer,
             blendColor(frame.palette.background, frame.palette.subdued, 0.45F));
    fillRect(renderer, frame.tempoX, frame.tracks[0].y - 5, usableWidth, 2);

    std::array<std::size_t, pocket::TrackCount> stepIndex{};
    for (std::size_t index = 0; index < drawList.commandCount; ++index) {
        const auto& command = drawList.commands[index];
        if (command.type == pocket::ui::MainDrawCommandType::Text) {
            if (command.textRole == pocket::ui::MainTextRole::Tempo) {
                drawText(renderer, mainTextFor(command), command.x,
                         frame.tempoY + 2, 180, headerAreaH / 2,
                         frame.palette.primary, headerScale);
            } else if (command.textRole == pocket::ui::MainTextRole::Transport) {
                const auto transportColor =
                    command.playing ? frame.palette.accent : frame.palette.subdued;
                const std::string trText =
                    std::string(command.playing ? "> PLAY  " : "|| STOP  ") +
                    std::to_string(command.masterTickPosition);
                drawText(renderer, trText, command.x,
                         frame.tempoY + headerAreaH / 2, 200,
                         headerAreaH / 2 - 4, transportColor, 1);
            } else {
                // Pure abstract geometric instrument icon (no T1..T8 text!)
                drawInstrumentGlyph(renderer, frame.palette, command.instrument,
                                    command.selected, command.muted, command.x,
                                    command.y, command.width, command.height);
            }
            continue;
        }
        if (command.type != pocket::ui::MainDrawCommandType::StepBar) {
            continue;
        }
        const auto trackIdx =
            static_cast<std::size_t>(std::max<int>(0, command.trackNumber - 1));
        const std::size_t curStep = stepIndex[trackIdx]++;
        float light = 0.0F;
        if (engine != nullptr && trackIdx < pocket::TrackCount) {
            light = engine->luminance(trackIdx, curStep);
        }

        // Equal-height full-lane geometric tiles:
        // Active steps and rest steps share the exact same vertical height (command.y, command.height).
        const std::int32_t bx = command.x;
        const std::int32_t by = command.y;
        const std::int32_t bw = command.width;
        const std::int32_t bh = command.height;

        auto fillColor = command.color;
        if (command.muted) {
            fillColor = command.active
                            ? blendColor(frame.palette.background,
                                         frame.palette.subdued, 0.42F)
                            : blendColor(frame.palette.background,
                                         frame.palette.subdued, 0.14F);
        } else if (command.next) {
            fillColor = command.active
                            ? frame.palette.accent
                            : blendColor(frame.palette.background,
                                         frame.palette.accent, 0.38F);
        } else if (command.active) {
            fillColor = command.selected
                            ? blendColor(frame.palette.primary,
                                         frame.palette.accent, 0.32F)
                            : frame.palette.primary;
        } else {
            // Rest step: flat matte tile of identical height
            fillColor = blendColor(frame.palette.background,
                                   frame.palette.subdued,
                                   command.selected ? 0.28F : 0.18F);
        }
        if (light > 0.0F && !command.muted) {
            fillColor = blendColor(fillColor, frame.palette.accent, light);
        }

        setColor(renderer, fillColor);
        fillRect(renderer, bx, by, bw, bh);

        if (command.active && !command.muted) {
            // Crisp architectural top bar on active pulse blocks
            setColor(renderer,
                     command.next || command.selected
                         ? frame.palette.accent
                         : blendColor(frame.palette.primary,
                                      frame.palette.accent, 0.55F));
            fillRect(renderer, bx, by, bw, std::max<std::int32_t>(3, bh / 12));
        } else if (!command.active && bw >= 6 && bh >= 10) {
            // Subtle inner recessed geometric plane on rest tiles so the grid reads as tactile hardware pads
            const auto innerColor =
                blendColor(frame.palette.background, frame.palette.subdued,
                           command.selected ? 0.20F : 0.12F);
            setColor(renderer, innerColor);
            fillRect(renderer, bx + 2, by + 2, bw - 4, bh - 4);
            // Tiny center geometric dot inside rest tile
            setColor(renderer,
                     blendColor(frame.palette.background,
                                command.next ? frame.palette.accent
                                             : frame.palette.subdued,
                                command.next ? 0.85F : 0.35F));
            fillRect(renderer, bx + bw / 2 - 1, by + bh / 2 - 1, 2, 2);
        }

        // Playhead geometric frame on current step
        if (command.next) {
            setColor(renderer, frame.palette.accent);
            fillRect(renderer, bx, by, bw, 3);
            fillRect(renderer, bx, by + bh - 3, bw, 3);
        }
    }
    drawFooter(renderer, frame.palette, width, height);
}

// Elastic Full-Width Euclidean Step Explorer + 8-track Polymetric Alignment Matrix for EDIT screen
void drawEditEuclideanExplorer(SDL_Renderer* renderer,
                               const pocket::ui::ThemePalette& palette,
                               const pocket::Sequencer& sequencer,
                               const pocket::ui::UiController& ui,
                               const pocket::app::DesktopAudioEngine* engine,
                               std::int32_t x,
                               std::int32_t y,
                               std::int32_t width,
                               std::int32_t height) {
    const std::size_t selectedIdx =
        std::min(ui.state().selectedTrack, pocket::TrackCount - 1);
    const auto selectedTrack = ui.effectiveTrackState(selectedIdx);
    const bool livePlaying = engine != nullptr && sequencer.isPlaying();
    const auto displayRhythm =
        [&](std::size_t track) -> pocket::EffectiveRhythm {
        const auto fallback = ui.effectiveTrackState(track).effectiveRhythm;
        if (livePlaying) {
            const auto published = engine->modulation().read(track);
            if (published.valid) {
                return published.rhythm;
            }
        }
        return fallback;
    };
    const auto shownRhythm = displayRhythm(selectedIdx);
    const pocket::EuclideanConfig rhythm{
        shownRhythm.length,
        shownRhythm.fill,
        shownRhythm.offset,
    };
    if (!pocket::EuclideanGenerator::isValid(rhythm) || width <= 0 ||
        height <= 0) {
        return;
    }
    std::uint8_t shownPosition = selectedTrack.position;
    if (livePlaying) {
        const auto playhead = engine->playhead().read(selectedIdx);
        if (playhead.length != 0) {
            shownPosition =
                static_cast<std::uint8_t>(playhead.position % rhythm.length);
        }
    }
    const bool modulated = shownRhythm.length != selectedTrack.rhythm.length ||
                           shownRhythm.fill != selectedTrack.rhythm.fill ||
                           shownRhythm.offset != selectedTrack.rhythm.offset;

    // Panel background
    setColor(renderer, blendColor(palette.background, palette.subdued, 0.14F));
    fillRect(renderer, x, y, width, height);

    const std::int32_t pad = 10;
    const std::int32_t innerX = x + pad;
    const std::int32_t innerW = width - 2 * pad;

    // Top row: Summary banner
    const std::int32_t bannerH = 18;
    const float density =
        rhythm.length > 0
            ? (100.0F * static_cast<float>(rhythm.fill) /
               static_cast<float>(rhythm.length))
            : 0.0F;
    const std::string bannerLeft =
        std::string(modulated ? "MOD " : "") + "E(" +
        std::to_string(rhythm.fill) + "," +
        std::to_string(rhythm.length) + "," + std::to_string(rhythm.offset) +
        ")   DENSITY " + formatNumber("%.0f%%", density);
    drawText(renderer, bannerLeft, innerX, y + 4, innerW * 2 / 3, bannerH,
             palette.accent, 2);
    const std::string bannerRight =
        "STEP " + std::to_string(shownPosition + 1) + "/" +
        std::to_string(rhythm.length);
    drawTextRight(renderer, bannerRight, innerX + innerW * 2 / 3, y + 4,
                  innerW / 3, bannerH, palette.primary, 2);

    // Hero Equal-Height Elastic Step Bar for selected track
    const std::int32_t heroY = y + bannerH + 8;
    const std::int32_t heroH = std::max<std::int32_t>(42, height * 36 / 100);
    const auto length = static_cast<std::int32_t>(rhythm.length);
    for (std::uint8_t step = 0; step < rhythm.length; ++step) {
        const auto stepIdx = static_cast<std::int32_t>(step);
        const auto cellX0 = innerX + (stepIdx * innerW) / length;
        const auto cellX1 = innerX + ((stepIdx + 1) * innerW) / length;
        const auto cellW = std::max<std::int32_t>(1, cellX1 - cellX0);
        const auto gap = (step + 1 < rhythm.length)
                             ? std::clamp<std::int32_t>(cellW / 12, 2, 5)
                             : 0;
        const auto barW = std::max<std::int32_t>(1, cellW - gap);

        const bool active = pocket::EuclideanGenerator::isActive(rhythm, step);
        const bool isPlayhead =
            livePlaying && (shownPosition == step);

        // Equal vertical height for both active and rest steps!
        const auto color =
            isPlayhead
                ? (active ? palette.accent
                          : blendColor(palette.background, palette.accent, 0.40F))
                : (active ? blendColor(palette.primary, palette.accent, 0.28F)
                          : blendColor(palette.background, palette.subdued,
                                       0.24F));
        setColor(renderer, color);
        fillRect(renderer, cellX0, heroY, barW, heroH);

        if (active) {
            setColor(renderer, palette.accent);
            fillRect(renderer, cellX0, heroY, barW, 4);
        } else if (barW >= 6) {
            setColor(renderer,
                     blendColor(palette.background, palette.subdued, 0.16F));
            fillRect(renderer, cellX0 + 2, heroY + 2, barW - 4, heroH - 4);
            setColor(renderer,
                     blendColor(palette.background, palette.subdued, 0.40F));
            fillRect(renderer, cellX0 + barW / 2 - 1, heroY + heroH / 2 - 1, 2,
                     2);
        }

        if (isPlayhead) {
            setColor(renderer, palette.accent);
            fillRect(renderer, cellX0, heroY, barW, 3);
            fillRect(renderer, cellX0, heroY + heroH - 3, barW, 3);
        }
    }

    // Bottom section: 8-Track Polymetric Phase Matrix with compact geometric instrument markers (no T1..T8 text!)
    const std::int32_t matrixTop = heroY + heroH + 8;
    const std::int32_t matrixH =
        std::max<std::int32_t>(32, y + height - matrixTop - 6);
    const std::int32_t stripPitch = std::max<std::int32_t>(
        4, matrixH / static_cast<std::int32_t>(pocket::TrackCount));
    const std::int32_t iconColW = 18;
    const std::int32_t stripX = innerX + iconColW;
    const std::int32_t stripW = innerW - iconColW;

    for (std::size_t t = 0; t < pocket::TrackCount; ++t) {
        const auto tr = ui.effectiveTrackState(t);
        const auto trShown = displayRhythm(t);
        const pocket::EuclideanConfig trRhythm{
            trShown.length,
            trShown.fill,
            trShown.offset,
        };
        if (!pocket::EuclideanGenerator::isValid(trRhythm)) {
            continue;
        }
        const std::int32_t sy =
            matrixTop + static_cast<std::int32_t>(t) * stripPitch;
        const bool isSel = (t == selectedIdx);
        const std::int32_t sh = std::max<std::int32_t>(3, stripPitch - 2);

        // Mini geometric indicator on left (Sampler = 3 mini vertical bars, Synth = hollow square/block)
        const auto markerColor =
            isSel ? palette.accent
                  : (tr.muted ? blendColor(palette.background, palette.subdued, 0.35F)
                              : palette.subdued);
        setColor(renderer, markerColor);
        if (tr.instrument == pocket::InstrumentType::Sampler) {
            fillRect(renderer, innerX + 2, sy + 2, 2, std::max<std::int32_t>(1, sh - 2));
            fillRect(renderer, innerX + 6, sy + 1, 2, sh);
            fillRect(renderer, innerX + 10, sy + 2, 2, std::max<std::int32_t>(1, sh - 2));
        } else {
            fillRect(renderer, innerX + 2, sy + 1, 10, sh);
            if (sh >= 5) {
                setColor(renderer, palette.background);
                fillRect(renderer, innerX + 4, sy + 2, 6, sh - 2);
            }
        }

        const auto trLen = static_cast<std::int32_t>(trRhythm.length);
        for (std::uint8_t s = 0; s < trRhythm.length; ++s) {
            const auto sx0 =
                stripX + (static_cast<std::int32_t>(s) * stripW) / trLen;
            const auto sx1 =
                stripX + (static_cast<std::int32_t>(s + 1) * stripW) / trLen;
            const auto sw = std::max<std::int32_t>(1, sx1 - sx0 - 1);
            const bool sActive =
                pocket::EuclideanGenerator::isActive(trRhythm, s);
            const auto sColor =
                sActive ? (isSel ? palette.accent : palette.primary)
                        : blendColor(palette.background, palette.subdued,
                                     isSel ? 0.32F : 0.20F);
            setColor(renderer, sColor);
            fillRect(renderer, sx0, sy + 1, sw, sh);
        }

        if (livePlaying) {
            // Live playhead tick per track so the matrix shows every lane
            // moving with the audio thread, not just the selected one.
            const auto ph = engine->playhead().read(t);
            if (ph.length != 0) {
                const auto laneStep =
                    static_cast<std::int32_t>(ph.position % trRhythm.length);
                const std::int32_t px =
                    stripX + (laneStep * stripW) / trLen;
                setColor(renderer, palette.accent);
                fillRect(renderer, px, sy, 2, sh + 2);
            }
        }
    }
}

// Graphical Sound Visualizer Header: AD Envelope + Filter Passband + Synth Wave + Delay Echoes
void drawSoundVisualizer(SDL_Renderer* renderer,
                         const pocket::ui::ThemePalette& palette,
                         const pocket::TrackState& track,
                         const pocket::project::ProjectEffectState& effects,
                         bool playing,
                         std::uint64_t nowMs,
                         std::int32_t x,
                         std::int32_t y,
                         std::int32_t width,
                         std::int32_t height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const std::int32_t gap = 10;
    const std::int32_t cardW = (width - 2 * gap) / 3;
    const std::int32_t cardH = height;
    const bool plaits =
        track.synth.engineType == pocket::SynthEngineType::Plaits;
    // Seconds-scale drift so the preview moves only while transport runs.
    const float drift =
        playing ? static_cast<float>(nowMs % 8000) / 8000.0F : 0.0F;

    // Card 1: AD Envelope + Low-Pass Filter Passband
    {
        const std::int32_t cx = x;
        setColor(renderer, blendColor(palette.background, palette.subdued, 0.18F));
        fillRect(renderer, cx, y, cardW, cardH);

        drawText(renderer, "AD ENV & LPF", cx + 8, y + 4, cardW - 16, 14,
                 palette.accent, 1);

        const std::int32_t plotX = cx + 8;
        const std::int32_t plotY = y + 20;
        const std::int32_t plotW = cardW - 16;
        const std::int32_t plotH = cardH - 26;

        // Filter cutoff passband shading
        const float cutoffNorm = std::clamp(
            (std::log10(std::max(100.0F, track.filter.cutoffHz)) - 2.0F) /
                2.301F,
            0.05F, 1.0F);
        const std::int32_t passW =
            static_cast<std::int32_t>(std::round(cutoffNorm * plotW));
        setColor(renderer,
                 blendColor(palette.background,
                            track.filter.enabled ? palette.accent : palette.subdued,
                            track.filter.enabled ? 0.22F : 0.10F));
        fillRect(renderer, plotX, plotY, passW, plotH);

        // Draw AD envelope vertical slices
        const float attNorm = std::clamp(
            track.envelope.attackSeconds /
                pocket::AdEnvelopeSettings::MaxAttackSeconds,
            0.04F, 0.85F);
        const float decNorm = std::clamp(
            track.envelope.decaySeconds /
                pocket::AdEnvelopeSettings::MaxDecaySeconds,
            0.08F, 1.0F);
        const std::int32_t peakX =
            std::max<std::int32_t>(2, static_cast<std::int32_t>(attNorm * plotW * 0.45F));
        const std::int32_t endX = std::min<std::int32_t>(
            plotW,
            peakX + std::max<std::int32_t>(
                        4, static_cast<std::int32_t>(decNorm * plotW * 0.55F)));

        for (std::int32_t col = 0; col < plotW; col += 2) {
            float env = 0.0F;
            if (col <= peakX) {
                env = static_cast<float>(col) / static_cast<float>(peakX);
            } else if (col <= endX) {
                const float t = static_cast<float>(col - peakX) /
                                static_cast<float>(std::max(1, endX - peakX));
                env = std::pow(1.0F - t, 1.8F);
            }
            const std::int32_t bh =
                std::max<std::int32_t>(1, static_cast<std::int32_t>(env * plotH));
            setColor(renderer, col <= peakX ? palette.accent : palette.primary);
            fillRect(renderer, plotX + col, plotY + plotH - bh, 2, bh);
        }

        if (playing) {
            // Sweeping cursor across the AD shape while the transport runs.
            const std::int32_t cursor = std::clamp<std::int32_t>(
                static_cast<std::int32_t>(drift * static_cast<float>(plotW)), 0,
                plotW - 1);
            float env = 0.0F;
            if (cursor <= peakX) {
                env = static_cast<float>(cursor) / static_cast<float>(peakX);
            } else if (cursor <= endX) {
                const float t = static_cast<float>(cursor - peakX) /
                                static_cast<float>(std::max(1, endX - peakX));
                env = std::pow(1.0F - t, 1.8F);
            }
            setColor(renderer,
                     blendColor(palette.background, palette.accent, 0.30F));
            fillRect(renderer, plotX + cursor, plotY, 1, plotH);
            const std::int32_t ch =
                std::max<std::int32_t>(2, static_cast<std::int32_t>(env * plotH));
            setColor(renderer, palette.accent);
            fillRect(renderer, plotX + cursor - 2, plotY + plotH - ch - 2, 5, 5);
        }
    }

    // Card 2: Synth Oscillator Waveform Preview (Morph / Timbre / Color)
    {
        const std::int32_t cx = x + cardW + gap;
        setColor(renderer, blendColor(palette.background, palette.subdued, 0.18F));
        fillRect(renderer, cx, y, cardW, cardH);

        const std::string title =
            std::string(plaits ? "PLAITS  " : "VA SYNTH  ") +
            formatNumber("%.0f HZ", track.synth.pitchHz);
        drawText(renderer, title, cx + 8, y + 4, cardW - 16, 14,
                 palette.accent, 1);

        const std::int32_t plotX = cx + 8;
        const std::int32_t plotY = y + 20;
        const std::int32_t plotW = cardW - 16;
        const std::int32_t plotH = cardH - 26;
        const std::int32_t midY = plotY + plotH / 2;

        setColor(renderer, blendColor(palette.background, palette.subdued, 0.4F));
        fillRect(renderer, plotX, midY, plotW, 1);

        constexpr float TwoPi = 6.2831853F;
        for (std::int32_t col = 0; col < plotW; col += 2) {
            const float phase =
                static_cast<float>(col) / static_cast<float>(plotW) * 2.0F +
                drift * 2.0F;
            const float p = phase - std::floor(phase);
            const float sine = std::sin(p * TwoPi);
            const float saw = 2.0F * p - 1.0F;
            float sample = 0.0F;
            if (plaits) {
                // Three drifting partials stand in for the Plaits macro
                // oscillator so the card still reads as a living waveform.
                const float macro = 0.55F * std::sin(p * TwoPi +
                                                     track.synth.morph *
                                                         TwoPi);
                const float overtone = 0.30F * track.synth.color *
                                       std::sin(p * 4.0F * TwoPi +
                                                track.synth.timbre * TwoPi);
                const float edge = 0.15F * (1.0F - track.synth.color) *
                                   (2.0F * std::fmod(p * 3.0F, 1.0F) - 1.0F);
                sample = macro + overtone + edge;
            } else {
                const float pulse =
                    p < (0.15F + 0.70F * track.synth.timbre) ? 1.0F : -1.0F;
                sample =
                    (1.0F - track.synth.morph) * sine + track.synth.morph * saw;
                sample = 0.65F * sample + 0.35F * pulse * track.synth.color;
            }
            sample = std::clamp(sample, -1.0F, 1.0F);

            const std::int32_t sy =
                midY - static_cast<std::int32_t>(sample * (plotH * 0.42F));
            const std::int32_t top = std::min(midY, sy);
            const std::int32_t h = std::max<std::int32_t>(2, std::abs(sy - midY));
            setColor(renderer, plaits ? palette.accent : palette.primary);
            fillRect(renderer, plotX + col, top, 2, h);
        }
    }

    // Card 3: Stereo Delay Echo Tap Visualizer
    {
        const std::int32_t cx = x + (cardW + gap) * 2;
        const std::int32_t cw = width - (cardW + gap) * 2;
        setColor(renderer, blendColor(palette.background, palette.subdued, 0.18F));
        fillRect(renderer, cx, y, cw, cardH);

        const std::string title =
            "DELAY  " + std::to_string(effects.delayMilliseconds) + " MS";
        drawText(renderer, title, cx + 8, y + 4, cw - 16, 14, palette.accent, 1);

        const std::int32_t plotX = cx + 8;
        const std::int32_t plotY = y + 20;
        const std::int32_t plotW = cw - 16;
        const std::int32_t plotH = cardH - 26;

        const int tapCount = 8;
        const float timeSpacing = std::clamp(
            static_cast<float>(effects.delayMilliseconds) / 800.0F, 0.15F, 1.0F);
        const std::int32_t tapStep =
            std::max<std::int32_t>(8, static_cast<std::int32_t>((plotW / tapCount) * timeSpacing));
        const std::int32_t barW = std::max<std::int32_t>(5, tapStep - 4);

        float amp = std::max(0.15F, effects.delayMix);
        const int pulseTap =
            playing ? static_cast<int>(drift * static_cast<float>(tapCount))
                    : -1;
        for (int tap = 0; tap < tapCount; ++tap) {
            const std::int32_t tx = plotX + tap * tapStep;
            if (tx + barW > plotX + plotW) {
                break;
            }
            const std::int32_t th =
                std::max<std::int32_t>(3, static_cast<std::int32_t>(amp * plotH));
            setColor(renderer,
                     (tap == 0 || tap == pulseTap)
                         ? palette.accent
                         : blendColor(palette.subdued, palette.primary, amp));
            fillRect(renderer, tx, plotY + plotH - th, barW, th);
            amp *= std::clamp(effects.delayFeedback, 0.05F, 0.90F);
        }
    }
}

// Graphical LFO Waveform + Jitter Scatter Visualizer Header for MODULATION screen
void drawModulationVisualizer(SDL_Renderer* renderer,
                              const pocket::ui::ThemePalette& palette,
                              const pocket::TrackState& track,
                              const pocket::ui::LiveModulationState& live,
                              bool livePlaying,
                              std::int32_t x,
                              std::int32_t y,
                              std::int32_t width,
                              std::int32_t height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const auto& lfo = track.lfos[0];
    const auto& jitter = track.jitter;
    const bool follow = livePlaying && live.valid && lfo.enabled;
    const float livePhase = std::clamp(live.lfoPhase, 0.0F, 1.0F);
    const float liveValue = std::clamp(live.lfoValue, -1.0F, 1.0F);
    const std::int32_t gap = 10;
    const std::int32_t leftW = width * 62 / 100;
    const std::int32_t rightW = width - leftW - gap;

    // Left Card: LFO Waveform Cycle & Depth
    {
        setColor(renderer, blendColor(palette.background, palette.subdued, 0.18F));
        fillRect(renderer, x, y, leftW, height);

        const std::string lfoHeader =
            std::string("LFO 1 [") + (lfo.enabled ? "ACTIVE" : "BYPASS") +
            "]  DEPTH " + formatNumber("%+.0f", static_cast<double>(lfo.amount));
        drawText(renderer, lfoHeader, x + 8, y + 4, leftW - 16, 14,
                 lfo.enabled ? palette.accent : palette.subdued, 1);
        if (follow) {
            // Live readout of the waveform output the audio thread is using.
            drawTextRight(renderer,
                          formatNumber("NOW %+.2f", static_cast<double>(liveValue)),
                          x + 8, y + 4, leftW - 16, 14, palette.primary, 1);
        }

        const std::int32_t plotX = x + 8;
        const std::int32_t plotY = y + 20;
        const std::int32_t plotW = leftW - 16;
        const std::int32_t plotH = height - 26;
        const std::int32_t midY = plotY + plotH / 2;

        setColor(renderer, blendColor(palette.background, palette.subdued, 0.45F));
        fillRect(renderer, plotX, midY, plotW, 1);

        // Quarter-cycle guides keep the moving marker readable.
        setColor(renderer, blendColor(palette.background, palette.subdued, 0.28F));
        for (int q = 1; q < 4; ++q) {
            fillRect(renderer, plotX + plotW * q / 4, plotY, 1, plotH);
        }

        const float depthScale =
            std::clamp(std::abs(static_cast<float>(lfo.amount)) / 31.0F, 0.15F, 1.0F) *
            (lfo.amount < 0 ? -1.0F : 1.0F);
        constexpr float TwoPi = 6.2831853F;
        const auto waveAt = [&](float p) {
            p = std::clamp(p, 0.0F, 1.0F);
            float w = 0.0F;
            switch (lfo.waveform) {
            case pocket::LfoWaveform::Sine:
                w = std::sin(p * TwoPi);
                break;
            case pocket::LfoWaveform::Triangle:
                w = 1.0F - 4.0F * std::abs(std::round(p - 0.25F) - (p - 0.25F));
                break;
            case pocket::LfoWaveform::Square:
                w = p < 0.5F ? 1.0F : -1.0F;
                break;
            case pocket::LfoWaveform::SawUp:
                w = 2.0F * p - 1.0F;
                break;
            case pocket::LfoWaveform::SawDown:
                w = 1.0F - 2.0F * p;
                break;
            case pocket::LfoWaveform::SampleAndHold: {
                const int bucket = static_cast<int>(p * 8.0F);
                const float table[8] = {0.8F, -0.5F, 0.3F, -0.9F,
                                        0.6F, -0.2F, 0.9F, -0.7F};
                w = table[bucket & 7];
                break;
            }
            }
            return w * depthScale;
        };
        for (std::int32_t col = 0; col < plotW; col += 3) {
            const float p =
                static_cast<float>(col) / static_cast<float>(plotW);
            const float w = waveAt(p);
            const std::int32_t wy =
                midY - static_cast<std::int32_t>(w * (plotH * 0.42F));
            const std::int32_t top = std::min(midY, wy);
            const std::int32_t bh = std::max<std::int32_t>(2, std::abs(wy - midY));
            setColor(renderer,
                     lfo.enabled ? palette.accent : palette.subdued);
            fillRect(renderer, plotX + col, top, 2, bh);
        }

        if (follow) {
            // Scan line + dot: the same phase/value the audio thread used for
            // the current tick, so the display breathes with the sound.
            const std::int32_t markerX =
                plotX + std::clamp<std::int32_t>(
                             static_cast<std::int32_t>(livePhase * plotW), 0,
                             plotW - 1);
            const std::int32_t markerY = midY - static_cast<std::int32_t>(
                                                     liveValue * (plotH * 0.42F));
            setColor(renderer, blendColor(palette.background, palette.accent,
                                          0.35F));
            fillRect(renderer, markerX, plotY, 1, plotH);
            setColor(renderer, palette.accent);
            fillRect(renderer, markerX - 2, markerY - 2, 5, 5);
            // Fading tail traces the waveform behind the marker.
            for (int tail = 1; tail <= 5; ++tail) {
                const float past = std::fmod(
                    livePhase - 0.018F * static_cast<float>(tail) + 1.0F, 1.0F);
                const std::int32_t trailX =
                    plotX + static_cast<std::int32_t>(past * plotW);
                const std::int32_t trailY = midY - static_cast<std::int32_t>(
                                                        waveAt(past) *
                                                        (plotH * 0.42F));
                setColor(renderer,
                         blendColor(palette.background, palette.accent,
                                    0.50F - 0.08F * static_cast<float>(tail)));
                fillRect(renderer, trailX, trailY - 1, 2, 3);
            }
        }
    }

    // Right Card: Non-destructive Jitter Probability & Micro-shift Bars
    {
        const std::int32_t rx = x + leftW + gap;
        setColor(renderer, blendColor(palette.background, palette.subdued, 0.18F));
        fillRect(renderer, rx, y, rightW, height);

        const std::string jitHeader =
            std::string("JITTER [") + (jitter.enabled ? "ON" : "OFF") + "] " +
            std::to_string(jitter.intensity) + "%";
        drawText(renderer, jitHeader, rx + 8, y + 4, rightW - 16, 14,
                 jitter.enabled ? palette.accent : palette.subdued, 1);

        const std::int32_t plotX = rx + 8;
        const std::int32_t plotY = y + 20;
        const std::int32_t plotW = rightW - 16;
        const std::int32_t plotH = height - 26;
        const int bars = 12;
        const std::int32_t stepW = std::max<std::int32_t>(4, plotW / bars);
        const float strength =
            std::max(0.18F, static_cast<float>(jitter.intensity) / 100.0F);
        std::uint32_t hash = jitter.randomSeed ^ 0x9E3779B9U;
        for (int b = 0; b < bars; ++b) {
            hash = hash * 1664525U + 1013904223U;
            const float r = static_cast<float>((hash >> 16) & 0xFFU) / 255.0F;
            const std::int32_t bh = std::max<std::int32_t>(
                3, static_cast<std::int32_t>(r * strength * plotH));
            setColor(renderer,
                     jitter.enabled ? palette.primary : palette.subdued);
            fillRect(renderer, plotX + b * stepW, plotY + plotH - bh,
                      std::max<std::int32_t>(2, stepW - 3), bh);
        }
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
                std::int32_t topBannerReserve = 0,
                std::optional<pocket::InstrumentType> headerInstrument = std::nullopt,
                bool headerMuted = false,
                std::size_t columnCount = 1,
                std::size_t firstColumnRows = 0) {
    const std::int32_t margin = std::max<std::int32_t>(16, width / 24);
    const std::int32_t headerHeight = std::max<std::int32_t>(36, height / 13);
    const std::int32_t footerReserve = std::max<std::int32_t>(32, height / 16);
    const std::int32_t topY = std::max<std::int32_t>(10, height / 42);

    drawHeaderBar(renderer, palette, title, headerRight, margin, topY, width,
                  headerHeight, headerInstrument, headerMuted);

    std::int32_t contentTop =
        topY + headerHeight + std::max<std::int32_t>(6, height / 72) +
        topBannerReserve;

    const std::int32_t contentBottom = height - footerReserve;
    const std::int32_t availableHeight =
        std::max<std::int32_t>(24, contentBottom - contentTop);

    const std::size_t columns = std::clamp<std::size_t>(columnCount, 1, 2);
    const std::size_t split =
        columns == 1
            ? rows.size()
            : (firstColumnRows > 0
                   ? std::min(firstColumnRows, rows.size())
                   : (rows.size() + 1) / 2);
    const std::array<std::size_t, 2> columnStart{0, split};
    const std::array<std::size_t, 2> columnRows{split, rows.size() - split};
    const std::size_t maxRows =
        std::max<std::size_t>(1, std::max(columnRows[0], columnRows[1]));

    const std::int32_t maxPitch = std::max<std::int32_t>(40, height / 12);
    const std::int32_t pitch = std::clamp<std::int32_t>(
        availableHeight / static_cast<std::int32_t>(maxRows), 18, maxPitch);
    const std::int32_t blockHeight =
        pitch * static_cast<std::int32_t>(maxRows);
    const std::int32_t startY =
        contentTop + std::max<std::int32_t>(0, (availableHeight - blockHeight) / 2);

    const std::int32_t usableWidth = width - 2 * margin;
    const std::int32_t columnGap =
        columns == 2 ? std::max<std::int32_t>(20, usableWidth * 4 / 100) : 0;
    const std::int32_t columnWidth =
        (usableWidth - columnGap * static_cast<std::int32_t>(columns - 1)) /
        static_cast<std::int32_t>(columns);
    const std::int32_t labelWidth =
        columns == 2 ? columnWidth * 32 / 100 : usableWidth * 24 / 100;
    const std::int32_t barXOffset =
        labelWidth + columnWidth * 2 / 100;
    const std::int32_t barWidth =
        columns == 2 ? columnWidth * 36 / 100 : usableWidth * 54 / 100;
    const std::int32_t valueXOffset =
        barXOffset + barWidth + columnWidth * 2 / 100;
    const std::int32_t valueWidth = columnWidth - valueXOffset;

    const std::int32_t rowScale =
        std::clamp<std::int32_t>(pitch / (pocket::app::GlyphHeight * 2), 1, 3);

    if (columns == 2) {
        // Thin divider clarifies the two parameter groups without a box.
        setColor(renderer, blendColor(palette.background, palette.subdued, 0.35F));
        fillRect(renderer, margin + columnWidth + columnGap / 2, startY, 1,
                 blockHeight);
    }

    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        const bool selected = index == selectedRow;
        const std::size_t column =
            (columns == 2 && index >= columnStart[1]) ? 1 : 0;
        const std::size_t localIndex = index - columnStart[column];
        const std::int32_t columnX =
            margin + static_cast<std::int32_t>(column) *
                         (columnWidth + columnGap);
        const std::int32_t y =
            startY + static_cast<std::int32_t>(localIndex) * pitch;
        const auto& labelColor = selected ? palette.accent : palette.primary;
        const auto& valueColor = selected ? palette.accent : palette.primary;

        if (selected) {
            setColor(renderer,
                     blendColor(palette.background, palette.subdued, 0.20F));
            fillRect(renderer, columnX - 4, y + 1, columnWidth + 8, pitch - 2);
            setColor(renderer, palette.accent);
            fillRect(renderer, columnX - 8, y + 2, 4, pitch - 4);
        }

        drawText(renderer, row.label, columnX, y, labelWidth, pitch,
                 labelColor, rowScale);

        if (row.barMode != BarMode::None) {
            drawFlatBar(renderer, palette, columnX + barXOffset, y, barWidth,
                        pitch, row.barMode, row.barRatio, selected,
                        row.activeState);
            drawTextRight(renderer, row.value, columnX + valueXOffset, y,
                          valueWidth, pitch, valueColor, rowScale);
        } else if (!row.value.empty()) {
            drawTextRight(renderer, row.value, columnX + barXOffset, y,
                          columnWidth - barXOffset, pitch, valueColor,
                          rowScale);
        }
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
    const std::int32_t margin = std::max<std::int32_t>(16, width / 24);
    const std::int32_t headerHeight = std::max<std::int32_t>(36, height / 13);
    const std::int32_t footerReserve = std::max<std::int32_t>(32, height / 16);
    const std::int32_t topY = std::max<std::int32_t>(10, height / 42);

    const std::size_t selected =
        std::min(ui.state().selectedTrack, pocket::TrackCount - 1);
    const auto field = static_cast<std::size_t>(ui.state().mixerField);
    const char* const fieldNames[] = {"VOLUME", "PAN",    "MUTE",
                                      "REV",    "DLY",    "MASTER"};
    const std::size_t fieldCount = sizeof(fieldNames) / sizeof(fieldNames[0]);

    const auto focusState = ui.effectiveTrackState(selected);
    std::string focusValue;
    switch (field) {
    case 0: focusValue = std::to_string(focusState.volume) + "%"; break;
    case 1: focusValue = formatNumber("%+.2f", focusState.pan); break;
    case 2: focusValue = focusState.muted ? "ON" : "OFF"; break;
    case 3:
        focusValue =
            std::to_string(static_cast<int>(focusState.reverbSend * 100.0F)) +
            "%";
        break;
    case 4:
        focusValue =
            std::to_string(static_cast<int>(focusState.delaySend * 100.0F)) +
            "%";
        break;
    default:
        focusValue =
            std::to_string(ui.systemSettings().audio.masterVolumePercent) + "%";
        break;
    }

    const std::string rightContext =
        "T" + std::to_string(selected + 1) + " " +
        fieldNames[std::min(field, fieldCount - 1)] + " " + focusValue +
        "  ENTER:NEXT  " + headerStatusFor(sequencer, ui);
    drawHeaderBar(renderer, palette, "MIXER CONSOLE", rightContext, margin, topY,
                  width, headerHeight);

    const std::int32_t contentTop =
        topY + headerHeight + std::max<std::int32_t>(8, height / 64);
    const std::int32_t availableHeight =
        std::max<std::int32_t>(120, height - footerReserve - contentTop);
    const std::int32_t legendHeight = std::max<std::int32_t>(11, height / 42);
    const std::int32_t pitch =
        std::max<std::int32_t>(20, (availableHeight - legendHeight) / 10);
    const std::int32_t rowScale =
        std::clamp<std::int32_t>(pitch / (pocket::app::GlyphHeight * 2), 1, 2);

    const std::int32_t usableWidth = width - 2 * margin;
    const std::int32_t gap = std::max<std::int32_t>(4, usableWidth / 160);
    const std::int32_t iconWidth =
        std::max<std::int32_t>(24, usableWidth * 4 / 100);
    const std::int32_t muteWidth =
        std::max<std::int32_t>(26, usableWidth * 5 / 100);
    const std::int32_t muteX = margin + iconWidth + gap;
    const std::int32_t volBarX = muteX + muteWidth + gap;
    const std::int32_t volBarW = usableWidth * 26 / 100;
    const std::int32_t panBarX = volBarX + volBarW + gap;
    const std::int32_t panBarW = usableWidth * 11 / 100;
    const std::int32_t revBarX = panBarX + panBarW + gap;
    const std::int32_t revBarW = usableWidth * 13 / 100;
    const std::int32_t dlyBarX = revBarX + revBarW + gap;
    const std::int32_t dlyBarW = usableWidth * 13 / 100;
    const std::int32_t textX = dlyBarX + dlyBarW + gap;
    const std::int32_t textWidth = margin + usableWidth - textX;

    const bool masterSelected = field == fieldCount - 1;

    // Column legend keeps the four fader strips readable without labels on
    // every row.
    const auto legendColor =
        blendColor(palette.background, palette.subdued, 0.80F);
    const std::int32_t legendY = contentTop;
    drawText(renderer, "VOL", volBarX, legendY, volBarW, legendHeight,
             legendColor);
    drawText(renderer, "PAN", panBarX, legendY, panBarW, legendHeight,
             legendColor);
    drawText(renderer, "REV", revBarX, legendY, revBarW, legendHeight,
             legendColor);
    drawText(renderer, "DLY", dlyBarX, legendY, dlyBarW, legendHeight,
             legendColor);

    std::int32_t y = contentTop + legendHeight + 2;
    for (std::size_t track = 0; track < pocket::TrackCount; ++track) {
        const auto state = ui.effectiveTrackState(track);
        const bool isSelected = (track == selected) && !masterSelected;
        const auto& color =
            isSelected ? palette.accent
                       : (state.muted ? palette.subdued : palette.primary);

        // Abstract geometric instrument icon instead of T1..T8 text
        drawInstrumentGlyph(renderer, palette, state.instrument, isSelected,
                            state.muted, margin, y, iconWidth, pitch);

        // Flat MUTE button: filled while engaged.
        const bool muteSelected = isSelected && field == 2;
        const auto muteFill =
            state.muted ? (isSelected ? palette.accent : palette.primary)
                        : blendColor(palette.background, palette.subdued,
                                     muteSelected ? 0.55F : 0.30F);
        setColor(renderer, muteFill);
        fillRect(renderer, muteX, y + 3, muteWidth, std::max<std::int32_t>(10, pitch - 6));
        const auto muteText =
            state.muted ? palette.background
                        : (muteSelected ? palette.accent : palette.primary);
        const std::int32_t muteScale =
            std::max<std::int32_t>(1, std::min(rowScale,
                                               fitScale("M", muteWidth, pitch)));
        drawText(renderer,
                 "M", muteX + (muteWidth - textPixelWidth("M", muteScale)) / 2,
                 y, muteWidth, pitch, muteText, muteScale);

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
                         static_cast<std::int32_t>(peak * volBarW), 3);
            }
        }

        drawFlatBar(renderer, palette, panBarX, y, panBarW, pitch,
                    BarMode::Bipolar, state.pan,
                    isSelected && field == 1, !state.muted);
        drawFlatBar(renderer, palette, revBarX, y, revBarW, pitch,
                    BarMode::Unipolar, state.reverbSend,
                    isSelected && field == 3, !state.muted);
        drawFlatBar(renderer, palette, dlyBarX, y, dlyBarW, pitch,
                    BarMode::Unipolar, state.delaySend,
                    isSelected && field == 4, !state.muted);

        std::string text = std::to_string(state.volume) + "%";
        if (state.muted) {
            text = "MUTED";
        } else {
            text += " R" +
                    std::to_string(
                        static_cast<int>(state.reverbSend * 100.0F)) +
                    " D" +
                    std::to_string(
                        static_cast<int>(state.delaySend * 100.0F));
        }
        drawTextRight(renderer, text, textX, y, textWidth, pitch, color,
                      rowScale);
        y += pitch;
    }

    setColor(renderer, blendColor(palette.background, palette.subdued, 0.45F));
    fillRect(renderer, margin, y + 2, usableWidth, 2);
    y += std::max<std::int32_t>(6, pitch / 5);

    const auto& masterColor = masterSelected ? palette.accent : palette.primary;
    // Master geometric double-bar glyph
    setColor(renderer, masterColor);
    fillRect(renderer, margin + 4, y + pitch / 4, iconWidth - 8, 4);
    fillRect(renderer, margin + 4, y + pitch / 2 + 2, iconWidth - 8, 4);

    const float masterRatio =
        static_cast<float>(ui.systemSettings().audio.masterVolumePercent) /
        100.0F;
    const std::int32_t masterBarW =
        dlyBarX + dlyBarW - volBarX;
    drawFlatBar(renderer, palette, volBarX, y, masterBarW, pitch,
                BarMode::Unipolar, masterRatio, masterSelected, true);
    if (engine != nullptr) {
        const float masterPeak = std::clamp(engine->masterPeak(), 0.0F, 1.0F);
        if (masterPeak > 0.0F) {
            setColor(renderer, palette.accent);
            fillRect(renderer, volBarX, y + pitch * 3 / 4,
                     static_cast<std::int32_t>(masterPeak * masterBarW), 3);
        }
    }
    drawTextRight(
        renderer,
        "MST " + std::to_string(ui.systemSettings().audio.masterVolumePercent) + "%",
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

    return {{"ELENG", std::to_string(track.rhythm.length) + " STEPS",
             BarMode::Unipolar, lengthRatio, true},
            {"EFILL", std::to_string(track.rhythm.fill) + " PULSES",
             BarMode::Unipolar, fillRatio, true},
            {"EOFFS", "+" + std::to_string(track.rhythm.offset) + " ROT",
             BarMode::Unipolar, offsetRatio, true},
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

std::vector<ScreenRow> soundRows(
    const pocket::TrackState& track,
    const pocket::project::ProjectEffectState& effects) {
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
    const float pitchRatio =
        (track.synth.pitchHz - pocket::TrackSynthSettings::MinPitchHz) /
        (pocket::TrackSynthSettings::MaxPitchHz -
         pocket::TrackSynthSettings::MinPitchHz);
    const float delayTimeRatio =
        static_cast<float>(effects.delayMilliseconds -
                           pocket::audio::DelayEffect::MinDelayMs) /
        static_cast<float>(pocket::audio::DelayEffect::MaxDelayMs -
                           pocket::audio::DelayEffect::MinDelayMs);
    const float delayFbRatio =
        effects.delayFeedback / pocket::audio::DelayEffect::MaxFeedback;

    return {{"ATTACK",
             formatNumber("%.0f MS", track.envelope.attackSeconds * 1000.0),
             BarMode::Unipolar, attackRatio, true},
            {"DECAY",
             formatNumber("%.0f MS", track.envelope.decaySeconds * 1000.0),
             BarMode::Unipolar, decayRatio, true},
            {"FILTER", track.filter.enabled ? "ON" : "OFF", BarMode::Discrete,
             0.0F, track.filter.enabled},
            {"CUTOFF", formatNumber("%.0f HZ", track.filter.cutoffHz),
             BarMode::Unipolar, cutoffRatio, track.filter.enabled},
            {"ENGINE",
             track.synth.engineType == pocket::SynthEngineType::Plaits
                 ? "PLAITS"
                 : "VA",
             BarMode::Discrete,
             track.synth.engineType == pocket::SynthEngineType::Plaits ? 1.0F
                                                                      : 0.0F,
             track.synth.engineType == pocket::SynthEngineType::Plaits},
            {"PITCH", formatNumber("%.0f HZ", track.synth.pitchHz),
             BarMode::Unipolar, pitchRatio, true},
            {"MORPH", formatNumber("%.0f%%", track.synth.morph * 100.0),
             BarMode::Unipolar, track.synth.morph, true},
            {"TIMBRE", formatNumber("%.0f%%", track.synth.timbre * 100.0),
             BarMode::Unipolar, track.synth.timbre, true},
            {"COLOR", formatNumber("%.0f%%", track.synth.color * 100.0),
             BarMode::Unipolar, track.synth.color, true},
            {"DLY TIME", std::to_string(effects.delayMilliseconds) + " MS",
             BarMode::Unipolar, delayTimeRatio, true},
            {"DLY FDBK",
             formatNumber("%.0f%%", effects.delayFeedback * 100.0),
             BarMode::Unipolar, delayFbRatio, true},
            {"DLY MIX", formatNumber("%.0f%%", effects.delayMix * 100.0),
             BarMode::Unipolar, effects.delayMix, true}};
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
            {"DEPTH", formatNumber("%+.0f", static_cast<double>(lfo.amount)),
             BarMode::Bipolar, amountRatio, true},
            {"JITTER", jitter.enabled ? "ON" : "OFF", BarMode::Discrete, 0.0F,
             jitter.enabled},
            {"STRENGTH", std::to_string(jitter.intensity) + "%",
             BarMode::Unipolar, jitterIntensityRatio, true},
            {"JIT MODE", jitterMode, BarMode::Unipolar,
             static_cast<float>(static_cast<int>(jitter.mode) + 1) / 4.0F,
             true},
            {"JIT RATE", rateIndex < 5 ? rates[rateIndex] : "?",
             BarMode::Unipolar, static_cast<float>(rateIndex + 1) / 5.0F, true},
            {"MAX SHIFT", std::to_string(jitter.shiftRange), BarMode::Unipolar,
             shiftRangeRatio, true},
            {"CST SEED", jitter.useCustomSeed ? "ON" : "OFF",
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
    const auto& fx = ui.effects();
    queue.push({AudioParameter::DelayTimeMs, 0,
                static_cast<float>(fx.delayMilliseconds)});
    queue.push({AudioParameter::DelayFeedback, 0, fx.delayFeedback});
    queue.push({AudioParameter::DelayMix, 0, fx.delayMix});
    for (std::size_t track = 0; track < pocket::TrackCount; ++track) {
        const auto& settings = ui.state().audioTrackSettings[track];
        const auto index = static_cast<std::uint8_t>(track);
        queue.push({AudioParameter::TrackVolume, index,
                    static_cast<float>(settings.volume)});
        queue.push({AudioParameter::TrackPan, index, settings.pan});
        queue.push(
            {AudioParameter::TrackReverbSend, index, settings.reverbSend});
        queue.push(
            {AudioParameter::TrackDelaySend, index, settings.delaySend});
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
        queue.push({AudioParameter::TrackSynthPitch, index,
                    settings.synth.pitchHz});
        queue.push({AudioParameter::TrackSynthMorph, index,
                    settings.synth.morph});
        queue.push({AudioParameter::TrackSynthTimbre, index,
                    settings.synth.timbre});
        queue.push({AudioParameter::TrackSynthColor, index,
                    settings.synth.color});
        queue.push({AudioParameter::TrackSynthEngineType, index,
                    static_cast<float>(settings.synth.engineType)});
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
    constexpr std::array<float, pocket::TrackCount> PresetPitches = {
        55.0F, 110.0F, 146.8F, 220.0F, 293.7F, 329.6F, 440.0F, 587.3F};
    constexpr std::array<float, pocket::TrackCount> PresetPans = {
        0.0F, -0.25F, 0.30F, -0.45F, 0.40F, -0.15F, 0.55F, -0.35F};
    constexpr std::array<std::uint8_t, pocket::TrackCount> PresetVolumes = {
        95, 85, 80, 90, 75, 85, 70, 80};
    for (std::size_t track = 0; track < pocket::TrackCount; ++track) {
        sequencer.setRhythm(track, pocket::ExamplePresetRhythms[track]);
        sequencer.setVolume(track, PresetVolumes[track]);
        sequencer.setPan(track, PresetPans[track]);
        if (track == 2 || track == 4 || track == 6) {
            sequencer.setInstrument(track, pocket::InstrumentType::Synth);
        }
        pocket::TrackSynthSettings syn{};
        syn.pitchHz = PresetPitches[track];
        syn.morph = 0.20F + 0.10F * static_cast<float>(track);
        syn.timbre = 0.35F + 0.07F * static_cast<float>(track);
        syn.color = 0.30F + 0.08F * static_cast<float>(track);
        sequencer.setSynth(track, syn);
        if (track == 0 || track == 2 || track == 3) {
            sequencer.setFilter(track, {true, 4800.0F + 1800.0F * static_cast<float>(track)});
        }
        pocket::LfoConfig lfo = sequencer.track(track).lfos[0];
        lfo.enabled = (track % 2 == 0);
        lfo.waveform = static_cast<pocket::LfoWaveform>(track % 6);
        lfo.amount = static_cast<std::int8_t>((track % 2 == 0 ? 1 : -1) * (10 + static_cast<int>(track) * 2));
        sequencer.setLfo(track, lfo, 0);
        pocket::JitterConfig jit = sequencer.track(track).jitter;
        jit.enabled = true;
        jit.intensity = static_cast<std::uint8_t>(25 + track * 6);
        sequencer.setJitter(track, jit);
    }
    const pocket::platform::SystemCapabilities capabilities{true,  true,  true,
                                                             true,  true,  true};
    pocket::ui::UiController ui(sequencer, capabilities);
    ui.resyncFromSequencer();
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

    const std::string projectPath =
        (std::filesystem::path(options.dataDir) / "project.pec").string();
    const auto applyToLive = [&](const pocket::project::ProjectData& data) {
        pocket::Sequencer scratch = sequencer;
        if (!pocket::project::applyProject(data, scratch)) {
            return false;
        }
        pocket::project::applyProject(data, sequencer);
        ui.resyncFromSequencer();
        ui.setEffects(data.effects);
        ui.setModified(false);
        if (audioRunning) {
            engine.syncSequencer(sequencer);
            pushAllTrackAudioParameters(ui, engine.parameterQueue());
        } else {
            engine.setEffectState(ui.effects());
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
                    projectPath,
                    pocket::project::captureProject(
                        sequencer, ui.state().audioTrackSettings, &ui.effects()),
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
        } else {
            engine.setEffectState(ui.effects());
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
        // Button-up events for a removed device never arrive.
        repeater.clear();
        selectHeld = false;
        startHeld = false;
        leftTrigger = false;
        rightTrigger = false;
    };
    const auto startTime = std::chrono::steady_clock::now();
    auto lastTick = startTime;

    // --inject-keys: automation hook used by the GUI checks. It pushes real
    // SDL key events into the queue so the adapter's press/release handling is
    // exercised exactly as it is for a physical keyboard.
    struct InjectedKeyEvent {
        std::uint64_t atMs = 0;
        bool down = true;
        bool repeat = false;
        SDL_Keycode keycode = SDLK_UNKNOWN;
    };

    const auto keycodeForName = [](const std::string& name)
        -> std::optional<SDL_Keycode> {
        using Name = std::pair<const char*, SDL_Keycode>;
        static constexpr Name Names[] = {
            {"up", SDLK_UP},         {"down", SDLK_DOWN},
            {"left", SDLK_LEFT},     {"right", SDLK_RIGHT},
            {"enter", SDLK_RETURN},  {"escape", SDLK_ESCAPE},
            {"space", SDLK_SPACE},   {"m", SDLK_m},
            {"q", SDLK_q},           {"e", SDLK_e},
            {"tab", SDLK_TAB},       {"pageup", SDLK_PAGEUP},
            {"pagedown", SDLK_PAGEDOWN},
            {"p", SDLK_p},           {"f1", SDLK_F1},
            {"shift", SDLK_LSHIFT},
        };
        for (const auto& entry : Names) {
            if (name == entry.first) {
                return entry.second;
            }
        }
        return std::nullopt;
    };

    std::vector<InjectedKeyEvent> injectedKeys;
    if (!options.injectKeys.empty()) {
        std::uint64_t atMs = 400;
        std::size_t tokenStart = 0;
        while (tokenStart <= options.injectKeys.size()) {
            const auto tokenEnd = options.injectKeys.find(',', tokenStart);
            const auto token = options.injectKeys.substr(
                tokenStart, tokenEnd == std::string::npos
                                ? std::string::npos
                                : tokenEnd - tokenStart);
            const auto colon = token.find(':');
            const auto name = token.substr(0, colon);
            const auto action = colon == std::string::npos
                                    ? std::string("tap")
                                    : token.substr(colon + 1);
            const auto keycode = keycodeForName(name);
            if (!keycode) {
                std::cerr << "Unknown --inject-keys key: " << name << '\n';
                return 1;
            }
            if (action == "tap") {
                injectedKeys.push_back({atMs, true, false, *keycode});
                injectedKeys.push_back({atMs + 80, false, false, *keycode});
            } else if (action == "down") {
                injectedKeys.push_back({atMs, true, false, *keycode});
            } else if (action == "up") {
                injectedKeys.push_back({atMs, false, false, *keycode});
            } else if (action == "repeat") {
                injectedKeys.push_back({atMs, true, true, *keycode});
            } else {
                std::cerr << "Unknown --inject-keys action: " << action << '\n';
                return 1;
            }
            atMs += 300;
            if (tokenEnd == std::string::npos) {
                break;
            }
            tokenStart = tokenEnd + 1;
        }
    }
    std::size_t nextInjectedKey = 0;

    while (running) {
        if (nextInjectedKey < injectedKeys.size()) {
            const auto elapsed = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime)
                    .count());
            while (nextInjectedKey < injectedKeys.size() &&
                   injectedKeys[nextInjectedKey].atMs <= elapsed) {
                const auto& inject = injectedKeys[nextInjectedKey];
                SDL_Event injected{};
                injected.type = inject.down ? SDL_KEYDOWN : SDL_KEYUP;
                injected.key.type = injected.type;
                injected.key.timestamp = SDL_GetTicks();
                injected.key.windowID = SDL_GetWindowID(window);
                injected.key.state =
                    inject.down ? SDL_PRESSED : SDL_RELEASED;
                injected.key.repeat = inject.repeat ? 1 : 0;
                injected.key.keysym.sym = inject.keycode;
                injected.key.keysym.scancode =
                    SDL_GetScancodeFromKey(inject.keycode);
                SDL_PushEvent(&injected);
                ++nextInjectedKey;
            }
        }

        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }

            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                 event.window.event == SDL_WINDOWEVENT_MINIMIZED)) {
                // A missed key-up (alt-tab, minimize, focus steal) must not
                // leave a direction repeating or a transport modifier stuck.
                repeater.clear();
                shiftHeld = false;
                selectHeld = false;
                startHeld = false;
                leftTrigger = false;
                rightTrigger = false;
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
                    // Edge-triggered actions must fire on the press edge only.
                    // Firing them on key-up as well would toggle twice (e.g.
                    // PlayStop would start on press and stop on release).
                    if (pocket::ui::InputMapping::shouldDispatch(
                            mapped->action, pressed, event.key.repeat != 0)) {
                        dispatchUi(*mapped);
                    }
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

            const std::int32_t margin = std::max<std::int32_t>(16, width / 24);
            const std::int32_t headerHeight = std::max<std::int32_t>(36, height / 13);
            const std::int32_t topY = std::max<std::int32_t>(10, height / 42);
            const std::int32_t bannerTop =
                topY + headerHeight + std::max<std::int32_t>(6, height / 72);
            const std::int32_t bannerW = width - 2 * margin;

            switch (ui.state().screen) {
            case pocket::ui::Screen::Main:
                renderMainScreen(renderer, sequencer, ui,
                                 audioRunning ? &engine : nullptr, width,
                                 height);
                break;
            case pocket::ui::Screen::Edit: {
                const std::int32_t explorerH = std::max<std::int32_t>(140, height * 28 / 100);
                renderRows(renderer, *palette, "EDIT",
                           headerStatus, editRows(selectedTrack),
                           static_cast<std::size_t>(ui.state().editField),
                           width, height, explorerH + 12,
                           selectedTrack.instrument, selectedTrack.muted);
                drawEditEuclideanExplorer(renderer, *palette, sequencer, ui,
                                          audioRunning ? &engine : nullptr,
                                          margin, bannerTop, bannerW,
                                          explorerH);
                break;
            }
            case pocket::ui::Screen::Sound: {
                const std::int32_t vizH = std::max<std::int32_t>(88, height * 15 / 100);
                renderRows(renderer, *palette, "SOUND",
                           headerStatus, soundRows(selectedTrack, ui.effects()),
                           static_cast<std::size_t>(ui.state().soundField),
                           width, height, vizH + 10,
                           selectedTrack.instrument, selectedTrack.muted, 2, 6);
                drawSoundVisualizer(renderer, *palette, selectedTrack,
                                    ui.effects(), sequencer.isPlaying(),
                                    SDL_GetTicks64(), margin, bannerTop, bannerW,
                                    vizH);
                break;
            }
            case pocket::ui::Screen::Mixer:
                renderMixerScreen(renderer, *palette, sequencer, ui,
                                  audioRunning ? &engine : nullptr, width,
                                  height);
                break;
            case pocket::ui::Screen::Modulation: {
                const std::int32_t vizH = std::max<std::int32_t>(84, height * 14 / 100);
                renderRows(renderer, *palette, "MODULATION",
                           headerStatus, modulationRows(selectedTrack),
                           static_cast<std::size_t>(ui.state().modulationField),
                           width, height, vizH + 10,
                           selectedTrack.instrument, selectedTrack.muted, 2, 6);
                pocket::ui::LiveModulationState live;
                const bool modPlaying = sequencer.isPlaying();
                if (modPlaying) {
                    if (audioRunning) {
                        live = engine.modulation().read(trackIndex);
                    } else {
                        // Visual-only fallback: the UI-side sequencer still
                        // advances its own LFO state when no audio device runs.
                        const auto shown = sequencer.lfoDisplay(trackIndex);
                        live.rhythm = selectedTrack.effectiveRhythm;
                        live.lfoPhase = static_cast<float>(shown.phase);
                        live.lfoValue = static_cast<float>(shown.value);
                        live.valid = true;
                    }
                }
                drawModulationVisualizer(renderer, *palette, selectedTrack, live,
                                         modPlaying, margin, bannerTop, bannerW,
                                         vizH);
                break;
            }
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
