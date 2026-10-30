#include "ui/UiController.h"
#include "ui/ThemePalette.h"

#include <algorithm>
#include <cmath>

namespace pocket::ui {
namespace {

constexpr std::size_t EditFieldCount = 6;
constexpr std::size_t SoundFieldCount = 14;
constexpr std::size_t ModulationFieldCount = 13;
constexpr std::size_t TuringFieldCount = 13;
constexpr std::size_t MixerFieldCount = 7;
constexpr std::size_t SettingsCategoryCount = 7;
constexpr std::size_t ProjectMenuItemCount = 5;
// Two-column parameter screens. Keep these in sync with the firstColumnRows
// values used by the SOUND and MODULATION renderers.
constexpr int SoundLeftColumnRows = 7;
constexpr int ModulationLeftColumnRows = 6;
constexpr int TuringLeftColumnRows = 6;

int adjacentColumnIndex(int index, int leftRows, int total,
                        int direction) noexcept {
    const int rightRows = total - leftRows;
    if (direction > 0) {
        if (index < leftRows && rightRows > 0) {
            return leftRows + std::min(index, rightRows - 1);
        }
    } else if (direction < 0) {
        if (index >= leftRows) {
            return std::min(index - leftRows, leftRows - 1);
        }
    }
    return index;
}

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
        if (state_.screen == Screen::Mixer && state_.mixerMasterRow) {
            state_.mixerMasterRow = false;
        }
        selectTrack(-1);
        return;
    }
    if (event.action == Action::TrackNext) {
        if (state_.screen == Screen::Mixer && state_.mixerMasterRow) {
            state_.mixerMasterRow = false;
        }
        selectTrack(1);
        return;
    }
    if (event.action == Action::MuteToggle) {
        if (state_.screen == Screen::Mixer && state_.mixerMasterRow) {
            return;
        }
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
    case Screen::Turing:
        dispatchTuring(event);
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
                // No file selector yet: the app layer handles this request
                // with a single project slot.
                requestProjectAction(ProjectRequest::SaveProjectAs);
                state_.modal = Modal::None;
                break;
            }
        }
        return;
    case Modal::FileSelector:
        dispatchFileBrowser(action);
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
    case Action::NavLeft:
    case Action::NavRight: {
        const int direction = event.action == Action::NavRight ? 1 : -1;
        const bool fine = event.fine || state_.fineAdjust;
        const double step = fine ? 1.0 : 5.0;
        const double nextTempo = std::clamp(
            sequencer_.tempo() + static_cast<double>(direction) * step,
            Sequencer::MinTempoBpm, Sequencer::MaxTempoBpm);
        if (sequencer_.setTempo(nextTempo)) {
            systemSettings_.clock.initialTempoBpm = nextTempo;
            state_.modified = true;
        }
        break;
    }
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
    case Action::NavRight: {
        const int direction = event.action == Action::NavRight ? 1 : -1;
        if (event.edit) {
            changeSoundValue(direction, event.fine || state_.fineAdjust);
        } else {
            moveSoundColumn(direction);
        }
        break;
    }
    case Action::Confirm:
        // Disabled rows never act, matching their subdued rendering.
        if (!soundFieldEnabled(state_.soundField)) {
            break;
        }
        if (state_.soundField == SoundField::FilterEnabled ||
            state_.soundField == SoundField::EngineType ||
            state_.soundField == SoundField::Model) {
            changeSoundValue(1, false);
        } else if (state_.soundField == SoundField::Sample) {
            openSampleBrowser();
        }
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

void UiController::moveSoundColumn(int direction) noexcept {
    state_.soundField = static_cast<SoundField>(adjacentColumnIndex(
        static_cast<int>(state_.soundField), SoundLeftColumnRows,
        static_cast<int>(SoundFieldCount), direction));
}

void UiController::moveModulationColumn(int direction) noexcept {
    state_.modulationField = static_cast<ModulationField>(adjacentColumnIndex(
        static_cast<int>(state_.modulationField), ModulationLeftColumnRows,
        static_cast<int>(ModulationFieldCount), direction));
}

void UiController::moveTuringColumn(int direction) noexcept {
    state_.turingField = static_cast<TuringField>(adjacentColumnIndex(
        static_cast<int>(state_.turingField), TuringLeftColumnRows,
        static_cast<int>(TuringFieldCount), direction));
}

void UiController::changeTuringValue(int direction, bool fine) {
    auto config = sequencer_.track(state_.selectedTrack).turing;
    const int step = fine ? 1 : 5;
    switch (state_.turingField) {
    case TuringField::Enabled:
        config.enabled = !config.enabled;
        break;
    case TuringField::Clock:
        config.clock = static_cast<TuringClockMode>(wrapIndex(
            static_cast<int>(config.clock) + direction, 3));
        break;
    case TuringField::Rate:
        config.rate = static_cast<TuringRate>(wrapIndex(
            static_cast<int>(config.rate) + direction,
            static_cast<std::size_t>(TuringRate::Count)));
        break;
    case TuringField::Length: {
        const int length = static_cast<int>(config.length) + direction;
        config.length = static_cast<std::uint8_t>(
            length < 2 ? 2 : length > 32 ? 32 : length);
        break;
    }
    case TuringField::Chance: {
        const int chance = static_cast<int>(config.chance) + direction * step;
        config.chance = static_cast<std::uint8_t>(
            chance < 0 ? 0 : chance > 100 ? 100 : chance);
        break;
    }
    case TuringField::Range: {
        const int range = static_cast<int>(config.range) + direction;
        config.range = static_cast<std::uint8_t>(
            range < 1 ? 1 : range > 24 ? 24 : range);
        break;
    }
    case TuringField::Scale:
        config.scale = static_cast<TuringScale>(wrapIndex(
            static_cast<int>(config.scale) + direction,
            static_cast<std::size_t>(TuringScale::Count)));
        break;
    case TuringField::Root:
        config.root = static_cast<std::uint8_t>(
            wrapIndex(static_cast<int>(config.root) + direction, 12));
        break;
    case TuringField::Glide: {
        const int glide = static_cast<int>(config.glide) + direction * step;
        config.glide = static_cast<std::uint8_t>(
            glide < 0 ? 0 : glide > 100 ? 100 : glide);
        break;
    }
    case TuringField::Drift: {
        const int drift = static_cast<int>(config.drift) + direction * step;
        config.drift = static_cast<std::uint8_t>(
            drift < 0 ? 0 : drift > 100 ? 100 : drift);
        break;
    }
    case TuringField::CustomSeed:
        config.useCustomSeed = !config.useCustomSeed;
        break;
    case TuringField::Seed: {
        const auto seed = static_cast<std::int64_t>(config.randomSeed) +
                          direction;
        config.randomSeed = static_cast<std::uint32_t>(
            seed < 0 ? 0 : seed > 65535 ? 65535 : seed);
        break;
    }
    case TuringField::Locked:
        config.locked = !config.locked;
        break;
    }
    if (sequencer_.setTuring(state_.selectedTrack, config)) {
        state_.modified = true;
    }
}

void UiController::dispatchTuring(ActionEvent event) {
    switch (event.action) {
    case Action::NavUp:
        state_.turingField = static_cast<TuringField>(
            wrapIndex(static_cast<int>(state_.turingField) - 1,
                      TuringFieldCount));
        break;
    case Action::NavDown:
        state_.turingField = static_cast<TuringField>(
            wrapIndex(static_cast<int>(state_.turingField) + 1,
                      TuringFieldCount));
        break;
    case Action::NavLeft:
    case Action::NavRight: {
        const int direction = event.action == Action::NavRight ? 1 : -1;
        if (event.edit) {
            changeTuringValue(direction, event.fine || state_.fineAdjust);
        } else {
            moveTuringColumn(direction);
        }
        break;
    }
    case Action::Confirm:
        // Confirm on the three switches toggles them. On every other row it
        // rolls a fresh phrase seed, the "throw the dice" gesture.
        if (state_.turingField == TuringField::Enabled ||
            state_.turingField == TuringField::CustomSeed ||
            state_.turingField == TuringField::Locked) {
            changeTuringValue(1, false);
        } else {
            (void)rollTuringSeed();
        }
        break;
    case Action::Back:
        state_.screen = Screen::Modulation;
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

std::uint32_t UiController::nextDice() noexcept {
    // xorshift32; only the UI thread touches this state.
    auto value = diceState_;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    diceState_ = value == 0 ? 0x2545F491U : value;
    return diceState_;
}

bool UiController::rollTuringSeed() noexcept {
    auto config = sequencer_.track(state_.selectedTrack).turing;
    // Stay inside the range the SEED row can also edit by hand, so a rolled
    // phrase is always reproducible from the displayed number.
    const std::uint32_t roll = nextDice();
    config.randomSeed = (roll ^ (roll >> 16)) & 0xFFFFU;
    config.useCustomSeed = true;
    if (!sequencer_.setTuring(state_.selectedTrack, config)) {
        return false;
    }
    // The seed is a real parameter, so the phrase can be reproduced later.
    state_.modified = true;
    return true;
}

bool UiController::soundFieldEnabled(SoundField field) const noexcept {
    const auto& source = sequencer_.track(state_.selectedTrack);
    const bool synth = source.instrument == InstrumentType::Synth;
    const auto track = audioSettings(state_.selectedTrack);
    switch (field) {
    case SoundField::FilterCutoff:
        return track.filter.enabled;
    case SoundField::EngineType:
        return synth;
    case SoundField::Model:
        return synth && track.synth.engineType == SynthEngineType::Plaits;
    case SoundField::Pitch:
    case SoundField::Morph:
    case SoundField::Timbre:
    case SoundField::Color:
        return synth;
    case SoundField::Sample:
        return source.instrument == InstrumentType::Sampler;
    default:
        return true;
    }
}

void UiController::changeSoundValue(int direction, bool fine) {
    // Subdued rows stay navigable but read-only, so neither Confirm nor the
    // A/Ctrl+arrow modifier can change their value.
    if (!soundFieldEnabled(state_.soundField)) {
        return;
    }
    const auto track = audioSettings(state_.selectedTrack);
    switch (state_.soundField) {
    case SoundField::Attack:
    case SoundField::Decay: {
        auto envelope = track.envelope;
        const float increment = fine ? 0.001F : 0.01F;
        if (state_.soundField == SoundField::Attack) {
            envelope.attackSeconds +=
                static_cast<float>(direction) * increment;
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
        return;
    }
    case SoundField::FilterEnabled:
    case SoundField::FilterCutoff: {
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
    case SoundField::Pitch:
    case SoundField::Morph:
    case SoundField::Timbre:
    case SoundField::Color: {
        auto synth = track.synth;
        audio::AudioParameter parameter =
            audio::AudioParameter::TrackSynthPitch;
        float value = 0.0F;
        if (state_.soundField == SoundField::Pitch) {
            const float step = fine ? 1.0F : 10.0F;
            synth.pitchHz = std::clamp(
                synth.pitchHz + static_cast<float>(direction) * step,
                TrackSynthSettings::MinPitchHz,
                TrackSynthSettings::MaxPitchHz);
            parameter = audio::AudioParameter::TrackSynthPitch;
            value = synth.pitchHz;
        } else if (state_.soundField == SoundField::Morph) {
            const float step = fine ? 0.01F : 0.05F;
            synth.morph = std::clamp(
                std::round((synth.morph + static_cast<float>(direction) * step) *
                           100.0F) /
                    100.0F,
                0.0F, 1.0F);
            parameter = audio::AudioParameter::TrackSynthMorph;
            value = synth.morph;
        } else if (state_.soundField == SoundField::Timbre) {
            const float step = fine ? 0.01F : 0.05F;
            synth.timbre = std::clamp(
                std::round((synth.timbre + static_cast<float>(direction) * step) *
                           100.0F) /
                    100.0F,
                0.0F, 1.0F);
            parameter = audio::AudioParameter::TrackSynthTimbre;
            value = synth.timbre;
        } else {
            const float step = fine ? 0.01F : 0.05F;
            synth.color = std::clamp(
                std::round((synth.color + static_cast<float>(direction) * step) *
                           100.0F) /
                    100.0F,
                0.0F, 1.0F);
            parameter = audio::AudioParameter::TrackSynthColor;
            value = synth.color;
        }
        if (!postAudioParameter(parameter, state_.selectedTrack, value)) {
            state_.audioParameterRejected = true;
            return;
        }
        if (audioParameterQueue_ != nullptr ||
            sequencer_.setSynth(state_.selectedTrack, synth)) {
            state_.modified = true;
        }
        return;
    }
    case SoundField::EngineType: {
        // Discrete two-value field: Left/Right and Confirm all toggle it, like
        // the filter-enable row, instead of clamping at an end value.
        auto synth = track.synth;
        const auto nextEngine =
            synth.engineType == SynthEngineType::Plaits
                ? SynthEngineType::BasicVa
                : SynthEngineType::Plaits;
        synth.engineType = nextEngine;
        if (!postAudioParameter(audio::AudioParameter::TrackSynthEngineType,
                                state_.selectedTrack,
                                nextEngine == SynthEngineType::Plaits ? 1.0F
                                                                      : 0.0F)) {
            state_.audioParameterRejected = true;
            return;
        }
        if (audioParameterQueue_ != nullptr ||
            sequencer_.setSynth(state_.selectedTrack, synth)) {
            state_.modified = true;
        }
        return;
    }
    case SoundField::Model: {
        auto synth = track.synth;
        const auto next = wrapIndex(
            static_cast<int>(synth.model) + direction,
            static_cast<std::size_t>(TrackSynthSettings::ModelCount));
        synth.model = static_cast<std::uint8_t>(next);
        if (!postAudioParameter(audio::AudioParameter::TrackSynthModel,
                                state_.selectedTrack,
                                static_cast<float>(next))) {
            state_.audioParameterRejected = true;
            return;
        }
        if (audioParameterQueue_ != nullptr ||
            sequencer_.setSynth(state_.selectedTrack, synth)) {
            state_.modified = true;
        }
        return;
    }
    case SoundField::Sample:
        // Confirm opens the WAV browser; Left/Right have no value.
        return;
    case SoundField::DelayTime: {
        const int step = fine ? 5 : 25;
        const int nextMs = std::clamp(
            static_cast<int>(state_.effects.delayMilliseconds) +
                direction * step,
            static_cast<int>(audio::DelayEffect::MinDelayMs),
            static_cast<int>(audio::DelayEffect::MaxDelayMs));
        if (!postAudioParameter(audio::AudioParameter::DelayTimeMs, 0,
                                static_cast<float>(nextMs))) {
            state_.audioParameterRejected = true;
            return;
        }
        state_.effects.delayMilliseconds = static_cast<std::uint16_t>(nextMs);
        state_.modified = true;
        return;
    }
    case SoundField::DelayFeedback: {
        const float step = fine ? 0.01F : 0.05F;
        const float nextFb = std::clamp(
            std::round((state_.effects.delayFeedback +
                        static_cast<float>(direction) * step) *
                       100.0F) /
                100.0F,
            0.0F, audio::DelayEffect::MaxFeedback);
        if (!postAudioParameter(audio::AudioParameter::DelayFeedback, 0,
                                nextFb)) {
            state_.audioParameterRejected = true;
            return;
        }
        state_.effects.delayFeedback = nextFb;
        state_.modified = true;
        return;
    }
    case SoundField::DelayMix: {
        const float step = fine ? 0.01F : 0.05F;
        const float nextMix = std::clamp(
            std::round((state_.effects.delayMix +
                        static_cast<float>(direction) * step) *
                       100.0F) /
                100.0F,
            0.0F, 1.0F);
        if (!postAudioParameter(audio::AudioParameter::DelayMix, 0, nextMix)) {
            state_.audioParameterRejected = true;
            return;
        }
        state_.effects.delayMix = nextMix;
        state_.modified = true;
        return;
    }
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
        if (!event.edit) {
            moveModulationColumn(direction);
            break;
        }
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
    case Action::NavDown: {
        // Rows are TRK1..TRK8 followed by the master strip, so up/down moves
        // the same way as the row movement on the parameter screens.
        const bool down = event.action == Action::NavDown;
        if (state_.mixerMasterRow) {
            state_.mixerMasterRow = false;
            state_.selectedTrack = down ? 0 : TrackCount - 1;
        } else if (down) {
            if (state_.selectedTrack + 1 >= TrackCount) {
                state_.mixerMasterRow = true;
            } else {
                ++state_.selectedTrack;
            }
        } else if (state_.selectedTrack == 0) {
            state_.mixerMasterRow = true;
        } else {
            --state_.selectedTrack;
        }
        break;
    }
    case Action::NavLeft:
    case Action::NavRight: {
        // Left/right walks the columns of the focused row. Values need A (or
        // Ctrl) held, exactly like SOUND, MODULATION, and TURING.
        const int direction = event.action == Action::NavRight ? 1 : -1;
        if (event.edit) {
            changeMixerValue(direction, event.fine || state_.fineAdjust);
        } else if (!state_.mixerMasterRow) {
            state_.mixerField = static_cast<MixerField>(wrapIndex(
                static_cast<int>(state_.mixerField) + direction,
                MixerFieldCount));
        }
        break;
    }
    case Action::Confirm:
        // A tap flips the switches, matching TURING. The bars only move while
        // A or Ctrl is held with left/right.
        if (!state_.mixerMasterRow &&
            (state_.mixerField == MixerField::Mute ||
             state_.mixerField == MixerField::Lock ||
             state_.mixerField == MixerField::Dice)) {
            changeMixerValue(1, false);
        }
        break;
    case Action::MuteToggle: {
        if (state_.mixerMasterRow) {
            break;
        }
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

void UiController::changeMixerValue(int direction, bool fine) {
    if (state_.mixerMasterRow) {
        const int step = fine ? 1 : 5;
        const int volume =
            static_cast<int>(systemSettings_.audio.masterVolumePercent) +
            direction * step;
        const auto bounded = static_cast<std::uint8_t>(
            volume < 0 ? 0 : volume > 100 ? 100 : volume);
        if (!postAudioParameter(audio::AudioParameter::MasterVolume, 0,
                                static_cast<float>(bounded))) {
            state_.audioParameterRejected = true;
            return;
        }
        systemSettings_.audio.masterVolumePercent = bounded;
        return;
    }

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
    case MixerField::Lock: {
        auto config = sequencer_.track(state_.selectedTrack).turing;
        config.locked = !config.locked;
        if (sequencer_.setTuring(state_.selectedTrack, config)) {
            state_.modified = true;
        }
        break;
    }
    case MixerField::Dice:
        // A dice has no up or down: either direction rolls a new phrase.
        (void)rollTuringSeed();
        break;
    case MixerField::ReverbSend:
    case MixerField::DelaySend: {
        const bool reverb = state_.mixerField == MixerField::ReverbSend;
        const float current = reverb ? track.reverbSend : track.delaySend;
        const float step = fine ? 0.01F : 0.05F;
        const float value = std::clamp(current + direction * step, 0.0F, 1.0F);
        if (!postAudioParameter(
                reverb ? audio::AudioParameter::TrackReverbSend
                       : audio::AudioParameter::TrackDelaySend,
                state_.selectedTrack, value)) {
            state_.audioParameterRejected = true;
            break;
        }
        if (audioParameterQueue_ != nullptr) {
            state_.modified = true;
        } else if (reverb ? sequencer_.setReverbSend(state_.selectedTrack,
                                                     value)
                          : sequencer_.setDelaySend(state_.selectedTrack,
                                                    value)) {
            state_.modified = true;
        }
        break;
    }
    }
}

void UiController::dispatchSystem(ActionEvent event) {
    if (event.action == Action::NavLeft || event.action == Action::NavRight) {
        const int direction = event.action == Action::NavRight ? 1 : -1;
        const bool fine = event.fine || state_.fineAdjust;
        if (state_.systemCategory ==
            platform::SettingsCategory::ClockAndTransport) {
            const double step = fine ? 1.0 : 5.0;
            const double nextTempo = std::clamp(
                systemSettings_.clock.initialTempoBpm +
                    static_cast<double>(direction) * step,
                Sequencer::MinTempoBpm, Sequencer::MaxTempoBpm);
            systemSettings_.clock.initialTempoBpm = nextTempo;
            sequencer_.setTempo(nextTempo);
        } else if (state_.systemCategory == platform::SettingsCategory::Midi) {
            // MIDI input is a global system preference, not project data.
            systemSettings_.midi.enabled = event.action == Action::NavRight;
        } else if (state_.systemCategory ==
                   platform::SettingsCategory::Display) {
            const auto nextTheme =
                adjacentTheme(systemSettings_.display.theme, direction);
            if (nextTheme.has_value()) {
                systemSettings_.display.theme = *nextTheme;
            }
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
        static_cast<Screen>(wrapIndex(static_cast<int>(state_.screen) + direction, 7));
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

    case EditField::Mute: {
        const auto track = audioSettings(state_.selectedTrack);
        if (!postAudioParameter(audio::AudioParameter::TrackMute,
                                state_.selectedTrack,
                                track.muted ? 0.0F : 1.0F)) {
            state_.audioParameterRejected = true;
            changed = false;
            break;
        }
        if (audioParameterQueue_ == nullptr) {
            sequencer_.setMuted(state_.selectedTrack, !track.muted);
        }
        changed = true;
        break;
    }
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
    return {source.volume, source.pan, source.reverbSend, source.delaySend,
            source.muted, source.envelope, source.filter, source.synth};
}

bool UiController::postAudioParameter(audio::AudioParameter parameter,
                                      std::size_t track,
                                      float value) noexcept {
    if (audioParameterQueue_ != nullptr &&
        !audioParameterQueue_->push(
            {parameter, static_cast<std::uint8_t>(track), value})) {
        return false;
    }
    if (parameter == audio::AudioParameter::MasterVolume ||
        parameter == audio::AudioParameter::DelayTimeMs ||
        parameter == audio::AudioParameter::DelayFeedback ||
        parameter == audio::AudioParameter::DelayMix) {
        return true;
    }
    auto& settings = state_.audioTrackSettings[track];
    switch (parameter) {
    case audio::AudioParameter::TrackVolume:
        settings.volume = static_cast<std::uint8_t>(value);
        break;
    case audio::AudioParameter::TrackPan:
        settings.pan = value;
        break;
    case audio::AudioParameter::TrackReverbSend:
        settings.reverbSend = value;
        break;
    case audio::AudioParameter::TrackDelaySend:
        settings.delaySend = value;
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
    case audio::AudioParameter::TrackSynthPitch:
        settings.synth.pitchHz = value;
        break;
    case audio::AudioParameter::TrackSynthMorph:
        settings.synth.morph = value;
        break;
    case audio::AudioParameter::TrackSynthTimbre:
        settings.synth.timbre = value;
        break;
    case audio::AudioParameter::TrackSynthColor:
        settings.synth.color = value;
        break;
    case audio::AudioParameter::TrackSynthEngineType:
        settings.synth.engineType = static_cast<SynthEngineType>(static_cast<int>(value));
        break;
    case audio::AudioParameter::TrackSynthModel:
        settings.synth.model = static_cast<std::uint8_t>(value);
        break;
    case audio::AudioParameter::DelayTimeMs:
    case audio::AudioParameter::DelayFeedback:
    case audio::AudioParameter::DelayMix:
    case audio::AudioParameter::MasterVolume:
        break;
    }
    return true;
}

void UiController::openProjectMenu() noexcept {
    state_.projectMenuItem = ProjectMenuItem::NewProject;
    state_.modal = Modal::ProjectMenu;
}

void UiController::openSampleBrowser() noexcept {
    fileBrowser_.active = true;
    fileBrowser_.cursor = 0;
    state_.modal = Modal::FileSelector;
    fileBrowserRequest_ = FileBrowserRequest::Scan;
}

void UiController::dispatchFileBrowser(Action action) noexcept {
    switch (action) {
    case Action::Back:
        fileBrowser_.active = false;
        state_.modal = Modal::None;
        return;
    case Action::NavUp:
    case Action::NavDown: {
        if (fileBrowser_.entries.empty()) {
            return;
        }
        const int direction = action == Action::NavDown ? 1 : -1;
        const int last = static_cast<int>(fileBrowser_.entries.size()) - 1;
        const int next = std::clamp(
            static_cast<int>(fileBrowser_.cursor) + direction, 0, last);
        fileBrowser_.cursor = static_cast<std::size_t>(next);
        return;
    }
    case Action::Confirm: {
        if (fileBrowser_.entries.empty()) {
            return;
        }
        const auto& entry = fileBrowser_.entries[fileBrowser_.cursor];
        if (entry.directory) {
            fileBrowser_.directory = entry.path;
            fileBrowser_.cursor = 0;
            fileBrowserRequest_ = FileBrowserRequest::Scan;
        } else {
            chosenSample_ = entry.path;
            chosenTrack_ = state_.selectedTrack;
            fileBrowser_.active = false;
            state_.modal = Modal::None;
        }
        return;
    }
    default:
        return;
    }
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
    requestProjectAction(state_.pendingProjectRequest);
    state_.pendingProjectRequest = ProjectRequest::None;
    state_.modal = Modal::None;
}

} // namespace pocket::ui
