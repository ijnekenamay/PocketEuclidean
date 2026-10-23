#include "ui/MainScreenDrawList.h"

#include <cmath>

namespace pocket::ui {
namespace {

bool isInside(const MainDrawCommand& command,
              std::int32_t width,
              std::int32_t height) noexcept {
    return command.x >= 0 && command.y >= 0 && command.width > 0 &&
           command.height > 0 && command.x <= width - command.width &&
           command.y <= height - command.height;
}

void append(MainScreenDrawList& drawList,
            const MainDrawCommand& command) noexcept {
    drawList.commands[drawList.commandCount++] = command;
}

} // namespace

bool buildMainScreenDrawList(const MainScreenFrame& frame,
                             MainScreenDrawList& drawList) noexcept {
    if (frame.width < 320 || frame.width > 16384 || frame.height < 240 ||
        frame.height > 16384 || frame.tempoX < 0 ||
        frame.tempoX >= frame.width || frame.tempoY < 0 ||
        frame.tempoY >= frame.height ||
        frame.patternX <= frame.tempoX || frame.patternX >= frame.width ||
        !std::isfinite(frame.tempoBpm) ||
        frame.tempoBpm < Sequencer::MinTempoBpm ||
        frame.tempoBpm > Sequencer::MaxTempoBpm || frame.rowPitch <= 0 ||
        frame.rowPitch > frame.height || frame.stepPitch <= 0) {
        return false;
    }

    MainScreenDrawList next;
    MainDrawCommand command;
    command.type = MainDrawCommandType::Clear;
    command.width = frame.width;
    command.height = frame.height;
    command.color = frame.palette.background;
    append(next, command);

    command = {};
    command.type = MainDrawCommandType::Text;
    command.textRole = MainTextRole::Tempo;
    command.x = frame.tempoX;
    command.y = frame.tempoY;
    command.width = frame.patternX - frame.tempoX;
    command.height = frame.rowPitch;
    command.color = frame.palette.primary;
    command.numericValue = frame.tempoBpm;
    if (!isInside(command, frame.width, frame.height)) {
        return false;
    }
    append(next, command);

    command = {};
    command.type = MainDrawCommandType::Text;
    command.textRole = MainTextRole::Transport;
    command.x = frame.tempoX;
    command.y = frame.tempoY + frame.rowPitch;
    command.width = frame.patternX - frame.tempoX;
    command.height = frame.rowPitch;
    command.color = frame.palette.subdued;
    command.playing = frame.playing;
    command.masterTickPosition = frame.masterTickPosition;
    if (!isInside(command, frame.width, frame.height)) {
        return false;
    }
    append(next, command);

    std::array<bool, TrackCount> seenTracks{};
    for (const auto& row : frame.tracks) {
        if (row.trackNumber == 0 || row.trackNumber > TrackCount ||
            seenTracks[row.trackNumber - 1] || row.stepCount == 0 ||
            row.stepCount > EuclideanGenerator::MaxLength || row.height <= 0 ||
            row.y < 0 || row.y > frame.height - row.height) {
            return false;
        }
        seenTracks[row.trackNumber - 1] = true;

        command = {};
        command.type = MainDrawCommandType::Text;
        command.textRole = MainTextRole::Track;
        command.x = frame.tempoX;
        command.y = row.y;
        command.width = frame.patternX - frame.tempoX;
        command.height = row.height;
        command.color = row.selected ? frame.palette.accent
                                     : frame.palette.primary;
        command.trackNumber = row.trackNumber;
        command.instrument = row.instrument;
        command.muted = row.muted;
        command.selected = row.selected;
        if (!isInside(command, frame.width, frame.height)) {
            return false;
        }
        append(next, command);

        for (std::uint8_t step = 0; step < row.stepCount; ++step) {
            const auto& bar = row.steps[step];
            command = {};
            command.type = MainDrawCommandType::StepBar;
            command.x = bar.x;
            command.y = bar.y;
            command.width = bar.width;
            command.height = bar.height;
            command.color = bar.active ? frame.palette.primary
                                       : frame.palette.subdued;
            command.trackNumber = row.trackNumber;
            command.active = bar.active;
            command.muted = row.muted;
            command.selected = row.selected;
            command.next = frame.playing && bar.next;
            if (!isInside(command, frame.width, frame.height)) {
                return false;
            }
            append(next, command);
        }
    }

    drawList = next;
    return true;
}

} // namespace pocket::ui
