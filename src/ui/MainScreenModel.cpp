#include "ui/MainScreenModel.h"

#include "sequencer/EuclideanGenerator.h"
#include "ui/UiController.h"

namespace pocket::ui {
namespace {

bool buildSnapshot(
    const Sequencer& sequencer,
    std::size_t selectedTrack,
    const std::array<TrackAudioSettings, TrackCount>* audioTrackSettings,
    const MainScreenLiveModulation* live,
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
        EffectiveRhythm effective = track.effectiveRhythm;
        if (live != nullptr && live->active && live->mirror != nullptr) {
            const auto published = live->mirror->read(trackIndex);
            if (published.valid) {
                effective = published.rhythm;
            }
        }
        const EuclideanConfig rhythm{effective.length, effective.fill,
                                     effective.offset};
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

    if (audioTrackSettings != nullptr) {
        for (std::size_t trackIndex = 0; trackIndex < TrackCount; ++trackIndex) {
            next.tracks[trackIndex].muted =
                (*audioTrackSettings)[trackIndex].muted;
        }
    }

    snapshot = next;
    return true;
}

} // namespace

bool makeMainScreenSnapshot(const Sequencer& sequencer,
                            std::size_t selectedTrack,
                            MainScreenSnapshot& snapshot) {
    return buildSnapshot(sequencer, selectedTrack, nullptr, nullptr, snapshot);
}

bool makeMainScreenSnapshot(
    const Sequencer& sequencer,
    std::size_t selectedTrack,
    const std::array<TrackAudioSettings, TrackCount>& audioTrackSettings,
    MainScreenSnapshot& snapshot) {
    return buildSnapshot(sequencer, selectedTrack, &audioTrackSettings, nullptr,
                         snapshot);
}

bool makeMainScreenSnapshot(
    const Sequencer& sequencer,
    std::size_t selectedTrack,
    const std::array<TrackAudioSettings, TrackCount>& audioTrackSettings,
    const MainScreenLiveModulation& live,
    MainScreenSnapshot& snapshot) {
    return buildSnapshot(sequencer, selectedTrack, &audioTrackSettings, &live,
                         snapshot);
}

} // namespace pocket::ui
