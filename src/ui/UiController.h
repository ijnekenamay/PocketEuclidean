#pragma once

#include "audio/AudioParameterQueue.h"
#include "sequencer/Sequencer.h"
#include "ui/InputMapping.h"
#include "platform/SystemSettings.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pocket::ui {

enum class Screen : std::uint8_t {
    Main,
    Edit,
    Sound,
    Mixer,
    Modulation,
    System
};

enum class ModulationField : std::uint8_t {
    Enabled,
    Waveform,
    RateMode,
    Rate,
    Destination,
    Amount,
    JitterEnabled,
    JitterIntensity,
    JitterMode,
    JitterRate,
    JitterShiftRange,
    JitterCustomSeed,
    JitterSeed
};

enum class MixerField : std::uint8_t {
    Volume,
    Pan,
    Mute,
    MasterVolume
};

enum class SoundField : std::uint8_t {
    Attack,
    Decay,
    FilterEnabled,
    FilterCutoff
};

enum class Modal : std::uint8_t {
    None,
    Help,
    ProjectMenu,
    FileSelector,
    ConfirmDiscard
};

enum class EditField : std::uint8_t {
    Length,
    Fill,
    Offset,
    Instrument,
    Volume,
    Mute
};

enum class ProjectMenuItem : std::uint8_t {
    NewProject,
    LoadProject,
    SaveProject,
    SaveProjectAs,
    Return
};

enum class ProjectRequest : std::uint8_t {
    None,
    NewProject,
    LoadProject,
    SaveProject,
    SaveProjectAs
};

enum class DiscardChoice : std::uint8_t {
    Save,
    Discard,
    Cancel
};

struct TrackAudioSettings {
    std::uint8_t volume = 100;
    float pan = 0.0F;
    bool muted = false;
    AdEnvelopeSettings envelope{};
    TrackFilterSettings filter{};
};

struct UiState {
    Screen screen = Screen::Main;
    Modal modal = Modal::None;
    std::size_t selectedTrack = 0;
    EditField editField = EditField::Length;
    SoundField soundField = SoundField::Attack;
    ModulationField modulationField = ModulationField::Enabled;
    MixerField mixerField = MixerField::Volume;
    platform::SettingsCategory systemCategory =
        platform::SettingsCategory::ClockAndTransport;
    ProjectMenuItem projectMenuItem = ProjectMenuItem::NewProject;
    ProjectRequest pendingProjectRequest = ProjectRequest::None;
    ProjectRequest projectRequest = ProjectRequest::None;
    DiscardChoice discardChoice = DiscardChoice::Cancel;
    bool modified = false;
    bool fineAdjust = false;
    bool audioParameterRejected = false;
    std::array<TrackAudioSettings, TrackCount> audioTrackSettings{};
};

class UiController {
public:
    explicit UiController(
        Sequencer& sequencer,
        platform::SystemCapabilities capabilities = {}) noexcept
        : sequencer_(sequencer), capabilities_(capabilities) {
        for (std::size_t track = 0; track < TrackCount; ++track) {
            const auto& source = sequencer_.track(track);
            state_.audioTrackSettings[track] = {
                source.volume, source.pan, source.muted, source.envelope,
                source.filter};
        }
    }

    const UiState& state() const noexcept { return state_; }
    const platform::SystemSettings& systemSettings() const noexcept {
        return systemSettings_;
    }
    void dispatch(ActionEvent event);
    void setAudioParameterQueue(
        audio::AudioParameterQueue* queue) noexcept {
        audioParameterQueue_ = queue;
        if (queue != nullptr) {
            for (std::size_t track = 0; track < TrackCount; ++track) {
                const auto& source = sequencer_.track(track);
                state_.audioTrackSettings[track] = {
                    source.volume, source.pan, source.muted, source.envelope,
                    source.filter};
            }
        }
    }
    ProjectRequest takeProjectRequest() noexcept;
    void setModified(bool modified) noexcept { state_.modified = modified; }

private:
    void dispatchModal(Action action);
    void dispatchMain(ActionEvent event);
    void dispatchEdit(ActionEvent event);
    void dispatchSound(ActionEvent event);
    void dispatchMixer(ActionEvent event);
    void dispatchModulation(ActionEvent event);
    void dispatchSystem(ActionEvent event);
    void switchScreen(int direction) noexcept;
    void selectTrack(int direction) noexcept;
    void moveEditField(int direction) noexcept;
    void changeEditValue(int direction, bool fine);
    void changeSoundValue(int direction, bool fine);
    TrackAudioSettings audioSettings(std::size_t track) const noexcept;
    bool postAudioParameter(audio::AudioParameter parameter,
                            std::size_t track,
                            float value) noexcept;
    void openProjectMenu() noexcept;
    void requestProjectAction(ProjectRequest request) noexcept;
    void resolveDiscardChoice() noexcept;
    void executePendingProjectRequest() noexcept;

    Sequencer& sequencer_;
    audio::AudioParameterQueue* audioParameterQueue_ = nullptr;
    platform::SystemCapabilities capabilities_{};
    platform::SystemSettings systemSettings_{};
    UiState state_{};
};

} // namespace pocket::ui
