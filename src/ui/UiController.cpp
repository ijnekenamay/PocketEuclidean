#include "ui/UiController.h"
#include "ui/ThemePalette.h"

#include <algorithm>

namespace pocket::ui {
namespace {

constexpr std::size_t EditFieldCount = 6;
constexpr std::size_t SoundFieldCount = 4;
constexpr std::size_t ModulationFieldCount = 13;
constexpr std::size_t MixerFieldCount = 4;
constexpr std::size_t SettingsCategoryCount = 7;
constexpr std::size_t ProjectMenuItemCount = 5;

std::size_t wrapIndex(int value, std::size_t count) noexcept {
    const auto signedCount = static_cast<int>(count);
    return static_cast<std::size_t>((value % signedCount + signedCount) % signedCount);
}

} // namespace

void UiController::dispatch(ActionEvent event) {
    state_.audioParameterRejected = false;
    if (event.action == Action::ValueFine) {
        state_.fineAdjust = event.fine;
        return;
    }
    if (event.action == Action::Back && state_.fineAdjust) {
        state_.fineAdjust = false;
        return;
    }

    if (state_.modal != Modal::None) {
        dispatchModal(event.action);
        return;
    }

    if (event.action == Action::TrackPrevious) {
        selectTrack(-1);
        return;
    }
    if (event.action == Action::TrackNext) {
        selectTrack(1);
        return;
    }
    if (event.action == Action::MuteToggle) {
        const auto track = audioSettings(state_.selectedTrack);
        if (!postAudioParameter(audio::AudioParameter::TrackMute,
                                state_.selectedTrack,
                                track.muted ? 0.0F : 1.0F)) {
            state_.audioParameterRejected = true;
            return;
        }
        if (audioParameterQueue_ == nullptr) {
            sequencer_.setMuted(state_.selectedTrack, !track.muted);
        }
        state_.modified = true;
        return;
    }

    if (event.action == Action::Help) {
        state_.modal = Modal::Help;
        return;
    }
    if (event.action == Action::ProjectMenu) {
        openProjectMenu();
        return;
    }

    switch (state_.screen) {
    case Screen::Main:
        dispatchMain(event);
        break;
    case Screen::Edit:
        dispatchEdit(event);
        break;
    case Screen::Sound:
        dispatchSound(event);
        break;
    case Screen::Mixer:
        dispatchMixer(event);
        break;
    case Screen::Modulation:
        dispatchModulation(event);
        break;
    case Screen::System:
        dispatchSystem(event);
        break;
    }
}

void UiController::dispatchModal(Action action) {
    switch (state_.modal) {
    case Modal::Help:
        if (action == Action::Back || action == Action::Confirm ||
            action == Action::Help) {
            state_.modal = Modal::None;
        }
        return;
    case Modal::ProjectMenu:
        if (action == Action::Back) {
            state_.modal = Modal::None;
        } else if (action == Action::NavUp || action == Action::NavDown) {
            const int direction = action == Action::NavDown ? 1 : -1;
            const auto index = wrapIndex(
                static_cast<int>(state_.projectMenuItem) + direction,
                ProjectMenuItemCount);
            state_.projectMenuItem = static_cast<ProjectMenuItem>(index);
        } else if (action == Action::Confirm) {
            switch (state_.projectMenuItem) {
            case ProjectMenuItem::NewProject:
                state_.pendingProjectRequest = ProjectRequest::NewProject;
                if (state_.modified) {
                    state_.discardChoice = DiscardChoice::Cancel;
                    state_.modal = Modal::ConfirmDiscard;
                } else {
                    executePendingProjectRequest();
                }
                break;
            case ProjectMenuItem::LoadProject:
                state_.pendingProjectRequest = ProjectRequest::LoadProject;
                if (state_.modified) {
                    state_.discardChoice = DiscardChoice::Cancel;
                    state_.modal = Modal::ConfirmDiscard;
                } else {
                    executePendingProjectRequest();
                }
                break;
            case ProjectMenuItem::SaveProject:
                requestProjectAction(ProjectRequest::SaveProject);
                state_.modal = Modal::None;
                break;
            case ProjectMenuItem::Return:
                state_.modal = Modal::None;
                break;
            case ProjectMenuItem::SaveProjectAs:
                requestProjectAction(ProjectRequest::SaveProjectAs);
                state_.modal = Modal::FileSelector;
                break;
            }
        }
        return;
    case Modal::FileSelector:
        if (action == Action::Back) {
            state_.modal = Modal::ProjectMenu;
        }
        return;
    case Modal::ConfirmDiscard:
        if (action == Action::Back) {
            state_.pendingProjectRequest = ProjectRequest::None;
            state_.modal = Modal::ProjectMenu;
        } else if (action == Action::NavUp || action == Action::NavDown) {
            const int direction = action == Action::NavDown ? 1 : -1;
            state_.discardChoice = static_cast<DiscardChoice>(wrapIndex(
                static_cast<int>(state_.discardChoice) + direction, 3));
        } else if (action == Action::Confirm) {
            resolveDiscardChoice();
        }
        return;
    case Modal::None:
        return;
    }
}

void UiController::dispatchMain(ActionEvent event) {
    switch (event.action) {
    case Action::NavUp:
    case Action::TrackPrevious:
        selectTrack(-1);
        break;
    case Action::NavDown:
    case Action::TrackNext:
        selectTrack(1);
        break;
    case Action::Confirm:
        state_.screen = Screen::Edit;
        break;
    case Action::PlayStop:
        if (sequencer_.isPlaying()) {
            sequencer_.stop();
        } else {
            sequencer_.start();
        }
        break;
    case Action::MuteToggle:
        sequencer_.setMuted(state_.selectedTrack,
                            !sequencer_.track(state_.selectedTrack).muted);
        state_.modified = true;
        break;
    case Action::ScreenPrevious:
        switchScreen(-1);
        break;
    case Action::ScreenNext:
        switchScreen(1);
        break;
    default:
        break;
    }
}

void UiController::dispatchEdit(ActionEvent event) {
    switch (event.action) {
    case Action::NavUp:
        moveEditField(-1);
        break;
    case Action::NavDown:
        moveEditField(1);
        break;
    case Action::NavLeft:
        changeEditValue(-1, event.fine || state_.fineAdjust);
        break;
    case Action::NavRight:
        changeEditValue(1, event.fine || state_.fineAdjust);
        break;
    case Action::Confirm:
        if (state_.editField == EditField::Mute) {
            const auto track = audioSettings(state_.selectedTrack);
            if (!postAudioParameter(audio::AudioParameter::TrackMute,
                                    state_.selectedTrack,
                                    track.muted ? 0.0F : 1.0F)) {
                state_.audioParameterRejected = true;
                break;
            }
            if (audioParameterQueue_ == nullptr) {
                sequencer_.setMuted(state_.selectedTrack, !track.muted);
            }
            state_.modified = true;
        } else if (state_.editField == EditField::Instrument) {
            const auto& track = sequencer_.track(state_.selectedTrack);
            const auto next = track.instrument == InstrumentType::Sampler
                                  ? InstrumentType::Synth
                                  : InstrumentType::Sampler;
            sequencer_.setInstrument(state_.selectedTrack, next);
            state_.modified = true;
        }
        break;
    case Action::Back:
        state_.screen = Screen::Main;
        break;
    case Action::MuteToggle: {
        const auto& track = sequencer_.track(state_.selectedTrack);
        sequencer_.setMuted(state_.selectedTrack, !track.muted);
        state_.modified = true;
        break;
    }
    case Action::ScreenPrevious:
        switchScreen(-1);
        break;
    case Action::ScreenNext:
        switchScreen(1);
        break;
    case Action::PlayStop:
        if (sequencer_.isPlaying()) {
            sequencer_.stop();
        } else {
            sequencer_.start();
        }
        break;
    default:
        break;
    }
}

void UiController::dispatchSound(ActionEvent event) {
    switch (event.action) {
    case Action::NavUp:
        state_.soundField = static_cast<SoundField>(
            wrapIndex(static_cast<int>(state_.soundField) - 1,
                      SoundFieldCount));
        break;
    case Action::NavDown:
        state_.soundField = static_cast<SoundField>(
            wrapIndex(static_cast<int>(state_.soundField) + 1,
                      SoundFieldCount));
        break;
    case Action::NavLeft:
        changeSoundValue(-1, event.fine || state_.fineAdjust);
        break;
    case Action::NavRight:
        changeSoundValue(1, event.fine || state_.fineAdjust);
        break;
    case Action::Back:
        state_.screen = Screen::Edit;
        break;
    case Action::ScreenPrevious:
    case Action::ScreenNext:
        switchScreen(event.action == Action::ScreenNext ? 1 : -1);
        break;
    case Action::PlayStop:
        if (sequencer_.isPlaying()) {
            sequencer_.stop();
        } else {
            sequencer_.start();
        }
        break;
    default:
        break;
    }
}

void UiController::changeSoundValue(int direction, bool fine) {
    const auto track = audioSettings(state_.selectedTrack);
    auto envelope = track.envelope;
    const float increment = fine ? 0.001F : 0.01F;
    if (state_.soundField == SoundField::FilterEnabled ||
        state_.soundField == SoundField::FilterCutoff) {
        auto filter = track.filter;
        if (state_.soundField == SoundField::FilterEnabled) {
            filter.enabled = !filter.enabled;
        } else {
            const float cutoffIncrement = fine ? 100.0F : 1000.0F;
            filter.cutoffHz +=
                static_cast<float>(direction) * cutoffIncrement;
            filter.cutoffHz = std::clamp(
                filter.cutoffHz, TrackFilterSettings::MinCutoffHz,
                TrackFilterSettings::MaxCutoffHz);
        }
        const auto parameter =
            state_.soundField == SoundField::FilterEnabled
                ? audio::AudioParameter::TrackFilterEnabled
                : audio::AudioParameter::TrackFilterCutoff;
        const float value = state_.soundField == SoundField::FilterEnabled
                                ? (filter.enabled ? 1.0F : 0.0F)
                                : filter.cutoffHz;
        if (!postAudioParameter(parameter, state_.selectedTrack, value)) {
            state_.audioParameterRejected = true;
            return;
        }
        if (audioParameterQueue_ != nullptr ||
            sequencer_.setFilter(state_.selectedTrack, filter)) {
            state_.modified = true;
        }
        return;
    }
    if (state_.soundField == SoundField::Attack) {
        envelope.attackSeconds += static_cast<float>(direction) * increment;
        envelope.attackSeconds = std::clamp(
            envelope.attackSeconds, AdEnvelopeSettings::MinAttackSeconds,
            AdEnvelopeSettings::MaxAttackSeconds);
    } else {
        envelope.decaySeconds +=
            static_cast<float>(direction) * increment * 10.0F;
        envelope.decaySeconds = std::clamp(
            envelope.decaySeconds, AdEnvelopeSettings::MinDecaySeconds,
            AdEnvelopeSettings::MaxDecaySeconds);
    }
    const auto parameter = state_.soundField == SoundField::Attack
                               ? audio::AudioParameter::TrackAttack
                               : audio::AudioParameter::TrackDecay;
    const float value = state_.soundField == SoundField::Attack
                            ? envelope.attackSeconds
                            : envelope.decaySeconds;
    if (!postAudioParameter(parameter, state_.selectedTrack, value)) {
        state_.audioParameterRejected = true;
        return;
    }
    if (audioParameterQueue_ != nullptr ||
        sequencer_.setEnvelope(state_.selectedTrack, envelope)) {
        state_.modified = true;
    }
}

void UiController::dispatchModulation(ActionEvent event) {
    switch (event.action) {
    case Action::NavUp:
        state_.modulationField = static_cast<ModulationField>(
            wrapIndex(static_cast<int>(state_.modulationField) - 1,
                      ModulationFieldCount));
        break;
    case Action::NavDown:
        state_.modulationField = static_cast<ModulationField>(
            wrapIndex(static_cast<int>(state_.modulationField) + 1,
                      ModulationFieldCount));
        break;
    case Action::NavLeft:
    case Action::NavRight: {
        const int direction = event.action == Action::NavRight ? 1 : -1;
        if (state_.modulationField >= ModulationField::JitterEnabled) {
            auto config = sequencer_.track(state_.selectedTrack).jitter;
            switch (state_.modulationField) {
            case ModulationField::JitterEnabled:
                config.enabled = !config.enabled;
                break;
            case ModulationField::JitterIntensity: {
                const int amount = config.intensity +
                                   direction * (event.fine || state_.fineAdjust
                                                    ? 1
                                                    : 5);
                config.intensity = static_cast<std::uint8_t>(
                    amount < 0 ? 0 : amount > 100 ? 100 : amount);
                break;
            }
            case ModulationField::JitterMode:
                config.mode = static_cast<JitterMode>(wrapIndex(
                    static_cast<int>(config.mode) + direction, 4));
                break;
            case ModulationField::JitterRate:
                config.rate = static_cast<JitterRate>(wrapIndex(
                    static_cast<int>(config.rate) + direction, 5));
                break;
            case ModulationField::JitterShiftRange: {
                const int range = config.shiftRange + direction;
                config.shiftRange = static_cast<std::uint8_t>(
                    range < 0 ? 0
                              : range > MaxJitterShiftTicks
                                    ? MaxJitterShiftTicks
                                    : range);
                break;
            }
            case ModulationField::JitterCustomSeed:
                config.useCustomSeed = !config.useCustomSeed;
                break;
            case ModulationField::JitterSeed: {
                const auto seed = static_cast<std::int64_t>(config.randomSeed) +
                                  direction;
                config.randomSeed = static_cast<std::uint32_t>(
                    seed < 0 ? 0 : seed > 65535 ? 65535 : seed);
                break;
            }
            default:
                break;
            }
            if (sequencer_.setJitter(state_.selectedTrack, config)) {
                state_.modified = true;
            }
            break;
        }

        auto config = sequencer_.track(state_.selectedTrack).lfos[0];
        switch (state_.modulationField) {
        case ModulationField::Enabled:
            config.enabled = !config.enabled;
            break;
        case ModulationField::Waveform:
            config.waveform = static_cast<LfoWaveform>(wrapIndex(
                static_cast<int>(config.waveform) + direction, 6));
            break;
        case ModulationField::RateMode:
            config.rateMode = config.rateMode == LfoRateMode::Free
                                  ? LfoRateMode::Synchronized
                                  : LfoRateMode::Free;
            break;
        case ModulationField::Rate:
            if (config.rateMode == LfoRateMode::Free) {
                config.rateHz += direction * (event.fine || state_.fineAdjust
                                                   ? 0.01
                                                   : 0.1);
                if (config.rateHz < ModulationEngine::MinFreeRateHz) {
                    config.rateHz = ModulationEngine::MinFreeRateHz;
                } else if (config.rateHz > ModulationEngine::MaxFreeRateHz) {
                    config.rateHz = ModulationEngine::MaxFreeRateHz;
                }
            } else {
                config.division = static_cast<LfoDivision>(wrapIndex(
                    static_cast<int>(config.division) + direction,
                    static_cast<int>(LfoDivision::Count)));
            }
            break;
        case ModulationField::Destination:
            config.destination = static_cast<ModulationDestination>(wrapIndex(
                static_cast<int>(config.destination) + direction, 3));
            break;
        case ModulationField::Amount: {
            const int step = (event.fine || state_.fineAdjust) ? 1 : 2;
            const int amount = config.amount + direction * step;
            config.amount = static_cast<std::int8_t>(
                amount < -31 ? -31 : amount > 31 ? 31 : amount);
            break;
        }
        default:
            break;
        }
        if (sequencer_.setLfo(state_.selectedTrack, config)) {
            state_.modified = true;
        }
        break;
    }
    case Action::Confirm: {
        if (state_.modulationField >= ModulationField::JitterEnabled) {
            auto config = sequencer_.track(state_.selectedTrack).jitter;
            if (state_.modulationField == ModulationField::JitterEnabled) {
                config.enabled = !config.enabled;
            } else if (state_.modulationField ==
                       ModulationField::JitterCustomSeed) {
                config.useCustomSeed = !config.useCustomSeed;
            }
            if (sequencer_.setJitter(state_.selectedTrack, config)) {
                state_.modified = true;
            }
        } else {
            auto config = sequencer_.track(state_.selectedTrack).lfos[0];
            if (state_.modulationField == ModulationField::Enabled) {
                config.enabled = !config.enabled;
            }
            if (sequencer_.setLfo(state_.selectedTrack, config)) {
                state_.modified = true;
            }
        }
        break;
    }
    case Action::Back:
        state_.screen = Screen::Sound;
        break;
    case Action::ScreenPrevious:
        switchScreen(-1);
        break;
    case Action::ScreenNext:
        switchScreen(1);
        break;
    case Action::PlayStop:
        if (sequencer_.isPlaying()) {
            sequencer_.stop();
        } else {
            sequencer_.start();
        }
        break;
    default:
        break;
    }
}

void UiController::dispatchMixer(ActionEvent event) {
    switch (event.action) {
    case Action::NavUp:
        selectTrack(-1);
        break;
    case Action::NavDown:
        selectTrack(1);
        break;
    case Action::NavLeft:
    case Action::NavRight: {
        const int direction = event.action == Action::NavRight ? 1 : -1;
        const bool fine = event.fine || state_.fineAdjust;
        const auto track = audioSettings(state_.selectedTrack);
        switch (state_.mixerField) {
        case MixerField::Volume: {
            const int step = fine ? 1 : 5;
            const int volume = static_cast<int>(track.volume) + direction * step;
            const auto bounded = static_cast<std::uint8_t>(
                volume < 0 ? 0 : volume > 100 ? 100 : volume);
            if (!postAudioParameter(audio::AudioParameter::TrackVolume,
                                    state_.selectedTrack,
                                    static_cast<float>(bounded))) {
                state_.audioParameterRejected = true;
                break;
            }
            if (audioParameterQueue_ != nullptr ||
                sequencer_.setVolume(state_.selectedTrack, bounded)) {
                state_.modified = true;
            }
            break;
        }
        case MixerField::Pan: {
            const float step = fine ? 0.01F : 0.1F;
            const float pan = std::clamp(track.pan + direction * step, -1.0F, 1.0F);
            if (!postAudioParameter(audio::AudioParameter::TrackPan,
                                    state_.selectedTrack, pan)) {
                state_.audioParameterRejected = true;
                break;
            }
            if (audioParameterQueue_ != nullptr ||
                sequencer_.setPan(state_.selectedTrack, pan)) {
                state_.modified = true;
            }
            break;
        }
        case MixerField::Mute:
            if (!postAudioParameter(audio::AudioParameter::TrackMute,
                                    state_.selectedTrack,
                                    track.muted ? 0.0F : 1.0F)) {
                state_.audioParameterRejected = true;
                break;
            }
            if (audioParameterQueue_ == nullptr) {
                sequencer_.setMuted(state_.selectedTrack, !track.muted);
            }
            state_.modified = true;
            break;
        case MixerField::MasterVolume: {
            const int step = fine ? 1 : 5;
            const int volume =
                static_cast<int>(systemSettings_.audio.masterVolumePercent) +
                direction * step;
            const auto bounded = static_cast<std::uint8_t>(
                volume < 0 ? 0 : volume > 100 ? 100 : volume);
            if (!postAudioParameter(audio::AudioParameter::MasterVolume, 0,
                                    static_cast<float>(bounded))) {
                state_.audioParameterRejected = true;
                break;
            }
            systemSettings_.audio.masterVolumePercent = bounded;
            break;
        }
        }
        break;
    }
    case Action::Confirm:
        state_.mixerField = static_cast<MixerField>(wrapIndex(
            static_cast<int>(state_.mixerField) + 1, MixerFieldCount));
        break;
    case Action::MuteToggle: {
        const auto track = audioSettings(state_.selectedTrack);
        if (!postAudioParameter(audio::AudioParameter::TrackMute,
                                state_.selectedTrack,
                                track.muted ? 0.0F : 1.0F)) {
            state_.audioParameterRejected = true;
            break;
        }
        if (audioParameterQueue_ == nullptr) {
            sequencer_.setMuted(state_.selectedTrack, !track.muted);
        }
        state_.modified = true;
        break;
    }
    case Action::Back:
        state_.screen = Screen::Main;
        break;
    case Action::ScreenPrevious:
        switchScreen(-1);
        break;
    case Action::ScreenNext:
        switchScreen(1);
        break;
    case Action::PlayStop:
        if (sequencer_.isPlaying()) {
            sequencer_.stop();
        } else {
            sequencer_.start();
        }
        break;
    default:
        break;
    }
}

void UiController::dispatchSystem(ActionEvent event) {
    if ((event.action == Action::NavLeft || event.action == Action::NavRight) &&
        state_.systemCategory == platform::SettingsCategory::Display) {
        const int direction = event.action == Action::NavRight ? 1 : -1;
        const auto nextTheme =
            adjacentTheme(systemSettings_.display.theme, direction);
        if (nextTheme.has_value()) {
            systemSettings_.display.theme = *nextTheme;
        }
    } else if (event.action == Action::NavUp ||
               event.action == Action::NavDown) {
        const int direction = event.action == Action::NavDown ? 1 : -1;
        auto index = static_cast<int>(state_.systemCategory);
        for (std::size_t attempt = 0; attempt < SettingsCategoryCount; ++attempt) {
            index = static_cast<int>(wrapIndex(index + direction,
                                               SettingsCategoryCount));
            const auto category = static_cast<platform::SettingsCategory>(index);
            if (platform::isCategoryAvailable(category, capabilities_)) {
                state_.systemCategory = category;
                break;
            }
        }
    } else if (event.action == Action::Back) {
        state_.screen = Screen::Main;
    } else if (event.action == Action::ScreenPrevious ||
               event.action == Action::ScreenNext) {
        switchScreen(event.action == Action::ScreenNext ? 1 : -1);
    } else if (event.action == Action::PlayStop) {
        if (sequencer_.isPlaying()) {
            sequencer_.stop();
        } else {
            sequencer_.start();
        }
    }
}

void UiController::switchScreen(int direction) noexcept {
    state_.screen =
        static_cast<Screen>(wrapIndex(static_cast<int>(state_.screen) + direction, 6));
}

void UiController::selectTrack(int direction) noexcept {
    state_.selectedTrack = wrapIndex(
        static_cast<int>(state_.selectedTrack) + direction, TrackCount);
}

void UiController::moveEditField(int direction) noexcept {
    state_.editField = static_cast<EditField>(
        wrapIndex(static_cast<int>(state_.editField) + direction, EditFieldCount));
}

void UiController::changeEditValue(int direction, bool fine) {
    const auto& current = sequencer_.track(state_.selectedTrack);
    auto rhythm = current.rhythm;
    bool changed = true;

    switch (state_.editField) {
    case EditField::Length: {
        const int next = static_cast<int>(rhythm.length) + direction;
        rhythm.length = static_cast<std::uint8_t>(next < 2 ? 2 : next > 32 ? 32 : next);
        if (rhythm.fill > rhythm.length) {
            rhythm.fill = rhythm.length;
        }
        sequencer_.setRhythm(state_.selectedTrack, rhythm);
        break;
    }
    case EditField::Fill: {
        const int next = static_cast<int>(rhythm.fill) + direction;
        rhythm.fill = static_cast<std::uint8_t>(
            next < 1 ? 1 : next > rhythm.length ? rhythm.length : next);
        sequencer_.setRhythm(state_.selectedTrack, rhythm);
        break;
    }
    case EditField::Offset: {
        const int next = static_cast<int>(rhythm.offset) + direction;
        rhythm.offset = static_cast<std::uint8_t>(next < 0 ? 0 : next > 31 ? 31 : next);
        sequencer_.setRhythm(state_.selectedTrack, rhythm);
        break;
    }
    case EditField::Instrument: {
        const auto instrument = direction < 0 ? InstrumentType::Sampler
                                              : InstrumentType::Synth;
        changed = sequencer_.setInstrument(state_.selectedTrack, instrument);
        break;
    }
    case EditField::Volume: {
        const int increment = fine ? 1 : 5;
        const auto audioState = audioSettings(state_.selectedTrack);
        const int next =
            static_cast<int>(audioState.volume) + direction * increment;
        const auto volume = static_cast<std::uint8_t>(
            next < 0 ? 0 : next > 100 ? 100 : next);
        if (!postAudioParameter(audio::AudioParameter::TrackVolume,
                                state_.selectedTrack,
                                static_cast<float>(volume))) {
            state_.audioParameterRejected = true;
            changed = false;
            break;
        }
        changed = audioParameterQueue_ != nullptr ||
                  sequencer_.setVolume(state_.selectedTrack, volume);
        break;
    }

    case EditField::Mute:
        changed = false;
        break;
    }

    if (changed) {
        state_.modified = true;
    }
}

TrackAudioSettings UiController::audioSettings(std::size_t track) const noexcept {
    if (audioParameterQueue_ != nullptr) {
        return state_.audioTrackSettings[track];
    }
    const auto& source = sequencer_.track(track);
    return {source.volume, source.pan, source.muted, source.envelope,
            source.filter};
}

bool UiController::postAudioParameter(audio::AudioParameter parameter,
                                      std::size_t track,
                                      float value) noexcept {
    if (audioParameterQueue_ != nullptr &&
        !audioParameterQueue_->push(
            {parameter, static_cast<std::uint8_t>(track), value})) {
        return false;
    }
    auto& settings = state_.audioTrackSettings[track];
    switch (parameter) {
    case audio::AudioParameter::TrackVolume:
        settings.volume = static_cast<std::uint8_t>(value);
        break;
    case audio::AudioParameter::TrackPan:
        settings.pan = value;
        break;
    case audio::AudioParameter::TrackMute:
        settings.muted = value == 1.0F;
        break;
    case audio::AudioParameter::TrackAttack:
        settings.envelope.attackSeconds = value;
        break;
    case audio::AudioParameter::TrackDecay:
        settings.envelope.decaySeconds = value;
        break;
    case audio::AudioParameter::TrackFilterEnabled:
        settings.filter.enabled = value == 1.0F;
        break;
    case audio::AudioParameter::TrackFilterCutoff:
        settings.filter.cutoffHz = value;
        break;
    case audio::AudioParameter::MasterVolume:
        break;
    }
    return true;
}

void UiController::openProjectMenu() noexcept {
    state_.projectMenuItem = ProjectMenuItem::NewProject;
    state_.modal = Modal::ProjectMenu;
}

ProjectRequest UiController::takeProjectRequest() noexcept {
    const auto request = state_.projectRequest;
    state_.projectRequest = ProjectRequest::None;
    return request;
}

void UiController::requestProjectAction(ProjectRequest request) noexcept {
    state_.projectRequest = request;
}

void UiController::resolveDiscardChoice() noexcept {
    switch (state_.discardChoice) {
    case DiscardChoice::Save:
        requestProjectAction(ProjectRequest::SaveProject);
        state_.pendingProjectRequest = ProjectRequest::None;
        state_.modal = Modal::None;
        break;
    case DiscardChoice::Discard:
        executePendingProjectRequest();
        break;
    case DiscardChoice::Cancel:
        state_.pendingProjectRequest = ProjectRequest::None;
        state_.modal = Modal::ProjectMenu;
        break;
    }
}

void UiController::executePendingProjectRequest() noexcept {
    if (state_.pendingProjectRequest == ProjectRequest::LoadProject) {
        state_.modal = Modal::FileSelector;
        return;
    }
    requestProjectAction(state_.pendingProjectRequest);
    state_.pendingProjectRequest = ProjectRequest::None;
    state_.modal = Modal::None;
}

} // namespace pocket::ui
