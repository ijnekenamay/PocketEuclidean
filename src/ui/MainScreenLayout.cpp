#include "ui/MainScreenLayout.h"

#include <algorithm>
#include <cmath>

namespace pocket::ui {
namespace {

constexpr std::int32_t MinViewportWidth = 320;
constexpr std::int32_t MinViewportHeight = 240;
constexpr std::int32_t MaxViewportDimension = 16384;

} // namespace

bool layoutMainScreen(const MainScreenSnapshot& snapshot,
                      platform::ThemeId theme,
                      std::int32_t width,
                      std::int32_t height,
                      MainScreenFrame& frame) noexcept {
    const auto* palette = paletteFor(theme);
    if (palette == nullptr || width < MinViewportWidth ||
        width > MaxViewportDimension || height < MinViewportHeight ||
        height > MaxViewportDimension || !std::isfinite(snapshot.tempoBpm) ||
        snapshot.tempoBpm < Sequencer::MinTempoBpm ||
        snapshot.tempoBpm > Sequencer::MaxTempoBpm) {
        return false;
    }

    const auto marginX = std::max<std::int32_t>(16, width / 16);
    const auto marginY = std::max<std::int32_t>(16, height / 16);
    const auto labelWidth = std::max<std::int32_t>(40, width / 12);
    const auto labelGap = std::max<std::int32_t>(8, width / 64);
    const auto headerHeight = std::max<std::int32_t>(42, height / 5);
    const auto rowsTop = marginY + headerHeight;
    const auto rowsBottom = height - marginY;
    const auto rowPitch = (rowsBottom - rowsTop) /
                          static_cast<std::int32_t>(TrackCount);
    const auto patternX = marginX + labelWidth + labelGap;
    const auto availablePatternWidth = width - marginX - patternX;
    const auto stepPitch = availablePatternWidth /
                           static_cast<std::int32_t>(
                               EuclideanGenerator::MaxLength);
    if (rowPitch <= 0 || stepPitch <= 0 || patternX >= width - marginX) {
        return false;
    }

    MainScreenFrame next;
    next.palette = *palette;
    next.width = width;
    next.height = height;
    next.tempoX = marginX;
    next.tempoY = marginY;
    next.patternX = patternX;
    next.rowPitch = rowPitch;
    next.stepPitch = stepPitch;
    next.tempoBpm = snapshot.tempoBpm;
    next.masterTickPosition = snapshot.masterTickPosition;
    next.playing = snapshot.playing;

    const auto barWidth = std::max<std::int32_t>(1, stepPitch * 2 / 5);
    const auto barHeight = std::max<std::int32_t>(1, rowPitch * 2 / 5);
    for (std::size_t trackIndex = 0; trackIndex < TrackCount; ++trackIndex) {
        const auto& source = snapshot.tracks[trackIndex];
        if (!EuclideanGenerator::isValid(source.rhythm) ||
            source.position >= source.rhythm.length) {
            return false;
        }

        auto& row = next.tracks[trackIndex];
        row.y = rowsTop +
                static_cast<std::int32_t>(trackIndex) * rowPitch;
        row.height = rowPitch;
        row.trackNumber = static_cast<std::uint8_t>(trackIndex + 1);
        row.stepCount = source.rhythm.length;
        row.selected = source.selected;
        row.muted = source.muted;
        row.instrument = source.instrument;

        for (std::uint8_t step = 0; step < source.rhythm.length; ++step) {
            const auto x = patternX +
                           static_cast<std::int32_t>(step) * stepPitch;
            auto& bar = row.steps[step];
            bar.x = x + (stepPitch - barWidth) / 2;
            bar.y = row.y + (rowPitch - barHeight) / 2;
            bar.width = barWidth;
            bar.height = barHeight;
            bar.active = source.activeSteps[step];
            bar.next = snapshot.playing && source.position == step;
        }
    }

    frame = next;
    return true;
}

} // namespace pocket::ui
