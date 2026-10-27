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
        std::uint32_t jitterEvents = 0;
        bool jitterValid = false;
        bool usedLiveRhythm = false;
        if (live != nullptr && live->active && live->mirror != nullptr) {
            const auto published = live->mirror->read(trackIndex);
            if (published.valid) {
                effective = published.rhythm;
                usedLiveRhythm = true;
                if (published.jitterValid) {
                    jitterEvents = published.jitterEvents;
                    jitterValid = true;
                }
            }
        }
        const bool liveOverlay = live != nullptr && live->active;
        if (!jitterValid && !liveOverlay && !usedLiveRhythm &&
            sequencer.isPlaying()) {
            // Visual-only playback: the UI-side sequencer advances its own
            // jitter state. When stopped, show the plain Euclidean pattern so
            // a stale mask from the last run is never displayed.
            jitterEvents = sequencer.jitterEventsMask(trackIndex, jitterValid);
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
                jitterValid ? ((jitterEvents >> step) & 1U) != 0
                            : EuclideanGenerator::isActive(rhythm, step);
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
