#include "ui/MainScreenModel.h"

#include "sequencer/EuclideanGenerator.h"
#include "ui/UiController.h"

namespace pocket::ui {

bool makeMainScreenSnapshot(const Sequencer& sequencer,
                            std::size_t selectedTrack,
                            MainScreenSnapshot& snapshot) {
    if (selectedTrack >= TrackCount) {
        return false;
    }

    MainScreenSnapshot next;
    next.tempoBpm = sequencer.tempo();
    next.masterTickPosition = sequencer.masterTickPosition();
    next.playing = sequencer.isPlaying();

    for (std::size_t trackIndex = 0; trackIndex < TrackCount; ++trackIndex) {
        const auto& track = sequencer.track(trackIndex);
        const EuclideanConfig rhythm{
            track.effectiveRhythm.length,
            track.effectiveRhythm.fill,
            track.effectiveRhythm.offset,
        };
        if (!EuclideanGenerator::isValid(rhythm) ||
            track.position >= rhythm.length) {
            return false;
        }

        auto& row = next.tracks[trackIndex];
        row.rhythm = rhythm;
        row.position = track.position;
        row.instrument = track.instrument;
        row.muted = track.muted;
        row.selected = trackIndex == selectedTrack;
        for (std::uint8_t step = 0; step < rhythm.length; ++step) {
            row.activeSteps[step] =
                EuclideanGenerator::isActive(rhythm, step);
        }
    }

    snapshot = next;
    return true;
}

bool makeMainScreenSnapshot(
    const Sequencer& sequencer,
    std::size_t selectedTrack,
    const std::array<TrackAudioSettings, TrackCount>& audioTrackSettings,
    MainScreenSnapshot& snapshot) {
    if (!makeMainScreenSnapshot(sequencer, selectedTrack, snapshot)) {
        return false;
    }
    for (std::size_t trackIndex = 0; trackIndex < TrackCount; ++trackIndex) {
        snapshot.tracks[trackIndex].muted =
            audioTrackSettings[trackIndex].muted;
    }
    return true;
}

} // namespace pocket::ui
