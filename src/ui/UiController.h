#pragma once

#include "audio/AudioParameterQueue.h"
#include "audio/DelayEffect.h"
#include "project/ProjectData.h"
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
    ReverbSend,
    DelaySend,
    MasterVolume
};

enum class SoundField : std::uint8_t {
    Attack,
    Decay,
    FilterEnabled,
    FilterCutoff,
    EngineType,
    Pitch,
    Morph,
    Timbre,
    Color,
    DelayTime,
    DelayFeedback,
    DelayMix
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
    float reverbSend = 0.0F;
    float delaySend = 0.0F;
    bool muted = false;
    AdEnvelopeSettings envelope{};
    TrackFilterSettings filter{};
    TrackSynthSettings synth{};
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
    project::ProjectEffectState effects{};
};

class UiController {
public:
    explicit UiController(
        Sequencer& sequencer,
        platform::SystemCapabilities capabilities = {}) noexcept
        : sequencer_(sequencer), capabilities_(capabilities) {
        systemSettings_.clock.initialTempoBpm = sequencer_.tempo();
        resyncFromSequencer();
    }

    const UiState& state() const noexcept { return state_; }
    const platform::SystemSettings& systemSettings() const noexcept {
        return systemSettings_;
    }
    const project::ProjectEffectState& effects() const noexcept {
        return state_.effects;
    }
    bool setEffects(const project::ProjectEffectState& effects) noexcept {
        if (!std::isfinite(effects.delayFeedback) ||
            !std::isfinite(effects.delayMix) ||
            effects.delayMilliseconds < audio::DelayEffect::MinDelayMs ||
            effects.delayMilliseconds > audio::DelayEffect::MaxDelayMs ||
            effects.delayFeedback < 0.0F ||
            effects.delayFeedback > audio::DelayEffect::MaxFeedback ||
            effects.delayMix < 0.0F || effects.delayMix > 1.0F) {
            return false;
        }
        state_.effects = effects;
        return true;
    }
    TrackState effectiveTrackState(std::size_t track) const noexcept {
        const std::size_t bounded =
            track < TrackCount ? track : (TrackCount - 1);
        TrackState effective = sequencer_.track(bounded);
        const auto& shadow = state_.audioTrackSettings[bounded];
        effective.volume = shadow.volume;
        effective.pan = shadow.pan;
        effective.reverbSend = shadow.reverbSend;
        effective.delaySend = shadow.delaySend;
        effective.muted = shadow.muted;
        effective.envelope = shadow.envelope;
        effective.filter = shadow.filter;
        effective.synth = shadow.synth;
        return effective;
    }
    void dispatch(ActionEvent event);
    void setAudioParameterQueue(
        audio::AudioParameterQueue* queue) noexcept {
        audioParameterQueue_ = queue;
        if (queue != nullptr) {
            resyncFromSequencer();
        }
    }
    // Re-reads the Sequencer after the project layer replaced its contents.
    void resyncFromSequencer() noexcept {
        for (std::size_t track = 0; track < TrackCount; ++track) {
            const auto& source = sequencer_.track(track);
            state_.audioTrackSettings[track] = {
                source.volume, source.pan, source.reverbSend,
                source.delaySend, source.muted, source.envelope,
                source.filter, source.synth};
        }
    }
    ProjectRequest takeProjectRequest() noexcept;
    // Restores persisted settings at startup; invalid settings are ignored.
    bool setSystemSettings(const platform::SystemSettings& settings) noexcept {
        if (!platform::isValid(settings)) {
            return false;
        }
        systemSettings_ = settings;
        sequencer_.setTempo(systemSettings_.clock.initialTempoBpm);
        return true;
    }
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
