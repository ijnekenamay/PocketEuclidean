#include "ui/InputMapping.h"
#include "ui/MainScreenDrawList.h"
#include "ui/MainScreenLayout.h"
#include "ui/MainScreenModel.h"
#include "ui/ModulationMirror.h"
#include "ui/StepLightBank.h"
#include "ui/StepLightEnvelope.h"
#include "ui/StepTriggerEventQueue.h"
#include "ui/ThemePalette.h"
#include "ui/UiController.h"
#include "platform/BackendInterfaces.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testKeyboardAndControllerMappings() {
    using namespace pocket::ui;

    expect(InputMapping::keyboard(KeyboardKey::Space)->action == Action::PlayStop,
           "Space maps to transport");
    expect(InputMapping::keyboard(KeyboardKey::Tab, true)->action ==
               Action::ScreenPrevious,
           "Shift+Tab switches to previous screen");
    expect(InputMapping::keyboard(KeyboardKey::Right, true)->fine,
           "Shift+arrow enables fine adjustment");
    expect(!InputMapping::keyboard(KeyboardKey::Right)->edit,
           "plain arrow does not carry the value-edit modifier");
    expect(InputMapping::keyboard(KeyboardKey::Right, false, false, true, true)
               ->edit,
           "Ctrl+arrow carries the tracker-style value-edit modifier");
    expect(!InputMapping::keyboard(KeyboardKey::Shift, false, false, false)->fine,
           "Shift release clears fine-adjust modifier");
    expect(!InputMapping::keyboard(KeyboardKey::Space, false, true),
           "OS key repeat cannot repeat transport toggles");
    expect(InputMapping::keyboard(KeyboardKey::Down, false, true).has_value(),
           "directional key repeat is allowed");
    expect(InputMapping::shouldDispatch(Action::PlayStop, true) &&
               !InputMapping::shouldDispatch(Action::PlayStop, false),
           "transport dispatches on the press edge only");
    expect(!InputMapping::shouldDispatch(Action::PlayStop, true, true),
           "OS key repeat cannot dispatch transport");
    expect(!InputMapping::shouldDispatch(Action::Confirm, false) &&
               !InputMapping::shouldDispatch(Action::Back, false) &&
               !InputMapping::shouldDispatch(Action::ScreenNext, false),
           "edge-triggered actions ignore the release edge");
    expect(InputMapping::shouldDispatch(Action::ValueFine, true) &&
               InputMapping::shouldDispatch(Action::ValueFine, false),
           "fine modifier tracks both press and release");
    expect(InputMapping::controller(ControllerControl::R1)->action ==
               Action::TrackNext,
           "controller shoulder maps to next track");

    auto profile = ControllerMappingProfile::defaults();
    expect(profile.bind(ControllerControl::A, Action::MuteToggle) &&
               InputMapping::controller(ControllerControl::A, profile)
                       ->action == Action::MuteToggle,
           "controller profile can override a logical control binding");
    expect(profile.bind(ControllerControl::Y, std::nullopt) &&
               !InputMapping::controller(ControllerControl::Y, profile),
           "controller profile can disable a control");
    expect(!profile.bind(static_cast<ControllerControl>(255), Action::Back) &&
               !InputMapping::controller(static_cast<ControllerControl>(255)) &&
               profile.isValid(),
           "unknown controller controls are rejected instead of mapping to Back");
    expect(!profile.bind(ControllerControl::A,
                        static_cast<Action>(255)) &&
               profile.actionFor(ControllerControl::A) == Action::MuteToggle,
           "invalid action bindings are rejected without replacing valid mappings");
}

void testAudioDeviceSpec() {
    using namespace pocket::platform;

    expect(isValid(AudioDeviceSpec{48000, 256}),
           "audio backend accepts the candidate 48 kHz / 256-frame spec");
    expect(isValid(AudioDeviceSpec{8000, 32}) &&
               isValid(AudioDeviceSpec{192000, 4096}),
           "audio backend accepts documented lower and upper spec bounds");
    expect(!isValid(AudioDeviceSpec{7999, 256}) &&
               !isValid(AudioDeviceSpec{192001, 256}) &&
               !isValid(AudioDeviceSpec{48000, 31}) &&
               !isValid(AudioDeviceSpec{48000, 4097}),
           "audio backend rejects specs outside bounded resource limits");
}

void testInputRepeater() {
    using namespace pocket::ui;
    InputRepeater repeater(300, 120, 60, 900);
    const ActionEvent down{Action::NavDown};
    expect(repeater.press(InputSource::Keyboard, 1, down, 1000).has_value(),
           "held direction fires immediately");
    expect(!repeater.poll(1299), "repeat waits through initial delay");
    expect(repeater.poll(1300).has_value(), "repeat fires at initial delay");
    expect(repeater.poll(1420).has_value(), "repeat follows the normal interval");
    expect(repeater.poll(1900).has_value(), "repeat fires during sustained hold");
    expect(!repeater.poll(1959), "accelerated repeat waits for its interval");
    expect(repeater.poll(1960).has_value(), "repeat accelerates after hold threshold");
    repeater.release(InputSource::Keyboard, 1);
    expect(!repeater.poll(2000), "released direction no longer repeats");

    const ActionEvent play{Action::PlayStop};
    repeater.press(InputSource::Controller, 2, play, 2100);
    expect(!repeater.poll(5000), "toggle action never repeats while held");

    InputRepeater simultaneous(100, 50, 25, 200);
    const ActionEvent up{Action::NavUp};
    const ActionEvent left{Action::NavLeft};
    expect(simultaneous.press(InputSource::Keyboard, 1, up, 0).has_value() &&
               simultaneous.press(InputSource::Keyboard, 2, left, 0).has_value(),
           "multiple held directions each emit their initial action");
    const auto repeatedKeyDown =
        simultaneous.press(InputSource::Keyboard, 1, up, 50);
    const auto beforeInitialDelay = simultaneous.poll(99);
    const auto firstRepeat = simultaneous.poll(100);
    const auto secondRepeat = simultaneous.poll(100);
    expect(!repeatedKeyDown && !beforeInitialDelay && firstRepeat &&
               firstRepeat->action == Action::NavUp && secondRepeat &&
               secondRepeat->action == Action::NavLeft,
           "duplicate key-down does not reset delay and simultaneous holds repeat fairly");
    simultaneous.release(InputSource::Keyboard, 1);
    const auto remainingRepeat = simultaneous.poll(150);
    expect(remainingRepeat && remainingRepeat->action == Action::NavLeft &&
               !simultaneous.poll(199) &&
               simultaneous.poll(200).has_value(),
           "released direction stops while remaining holds keep their repeat schedule");

    InputRepeater nearTimestampLimit(100, 50, 25, 200);
    const auto maxTimestamp = std::numeric_limits<std::uint64_t>::max();
    expect(nearTimestampLimit
                   .press(InputSource::Keyboard, 1, up, maxTimestamp - 50)
                   .has_value() &&
               !nearTimestampLimit.poll(maxTimestamp - 1) &&
               nearTimestampLimit.poll(maxTimestamp).has_value(),
           "repeat deadline addition saturates instead of wrapping timestamp");

    InputRepeater cleared(100, 50, 25, 200);
    expect(cleared.press(InputSource::Keyboard, 3, down, 0).has_value(),
           "direction is held before focus loss");
    cleared.clear();
    expect(!cleared.poll(1000) && !cleared.poll(2000),
           "clearing held controls stops repeats after focus loss");
}

void testStepLightEnvelope() {
    using namespace pocket::ui;

    StepLightEnvelope frequentlySampled;
    StepLightEnvelope sparselySampled;
    expect(frequentlySampled.trigger(2.0) && sparselySampled.trigger(2.0),
           "step illumination accepts matching timestamps in separate instances");
    float frequentValue = 0.0F;
    float sparseValue = 0.0F;
    expect(frequentlySampled.valueAt(2.01, frequentValue) &&
               frequentlySampled.valueAt(2.04, frequentValue) &&
               frequentlySampled.valueAt(2.08, frequentValue) &&
               frequentlySampled.valueAt(2.14, frequentValue) &&
               sparselySampled.valueAt(2.14, sparseValue) &&
               std::abs(frequentValue - sparseValue) < 1.0e-6F,
           "envelope luminance at a timestamp is independent of frame sampling rate");

    StepLightEnvelope arbitraryEpoch;
    float epochLuminance = -1.0F;
    expect(arbitraryEpoch.trigger(-5.0) &&
               arbitraryEpoch.valueAt(-5.0, epochLuminance) &&
               epochLuminance == 0.0F &&
               arbitraryEpoch.valueAt(
                   -5.0 + StepLightEnvelope::AttackSeconds, epochLuminance) &&
               epochLuminance == 1.0F,
           "step illumination supports monotonic clocks with negative epochs");

    StepLightEnvelope envelope;
    float luminance = -1.0F;
    expect(envelope.valueAt(1.0, luminance) && luminance == 0.0F,
           "step illumination begins dark before a trigger");
    expect(envelope.trigger(1.0), "step illumination accepts a valid trigger time");
    expect(envelope.valueAt(1.0, luminance) && luminance == 0.0F,
           "step illumination starts its attack at the trigger timestamp");

    expect(envelope.valueAt(1.0 + StepLightEnvelope::AttackSeconds * 0.5,
                            luminance) &&
               std::abs(luminance - 0.5F) < 1.0e-5F,
           "step illumination uses a smooth, elapsed-time attack");
    expect(envelope.valueAt(1.0 + StepLightEnvelope::AttackSeconds,
                            luminance) &&
               luminance == 1.0F,
           "step illumination reaches full level at the attack boundary");
    expect(envelope.valueAt(
               1.0 + StepLightEnvelope::AttackSeconds +
                   StepLightEnvelope::PeakHoldSeconds * 0.5,
               luminance) &&
               luminance == 1.0F,
           "step illumination holds its peak for the configured interval");

    const double decayStart =
        1.0 + StepLightEnvelope::AttackSeconds +
        StepLightEnvelope::PeakHoldSeconds;
    expect(envelope.valueAt(decayStart + StepLightEnvelope::DecaySeconds * 0.5,
                            luminance) &&
               std::abs(luminance - 0.5F) < 1.0e-5F,
           "step illumination decays smoothly from elapsed time");
    expect(envelope.valueAt(decayStart + StepLightEnvelope::DecaySeconds,
                            luminance) &&
               luminance == 0.0F,
           "step illumination reaches darkness at the decay boundary");
    expect(envelope.valueAt(decayStart + StepLightEnvelope::DecaySeconds + 1.0,
                            luminance) &&
               luminance == 0.0F,
           "step illumination remains dark after its envelope completes");

    expect(envelope.trigger(4.0), "step illumination accepts a later retrigger");
    const double retriggerAt =
        4.0 + StepLightEnvelope::AttackSeconds +
        StepLightEnvelope::PeakHoldSeconds +
        StepLightEnvelope::DecaySeconds * 0.5;
    expect(envelope.valueAt(retriggerAt, luminance),
           "step illumination evaluates before retrigger continuity check");
    const float beforeRetrigger = luminance;
    expect(envelope.trigger(retriggerAt) &&
               envelope.valueAt(retriggerAt, luminance) &&
               std::abs(luminance - beforeRetrigger) < 1.0e-5F,
           "retrigger continues from current luminance without a brightness jump");
    expect(envelope.valueAt(retriggerAt + StepLightEnvelope::AttackSeconds,
                            luminance) &&
               luminance == 1.0F,
           "retrigger smoothly returns the step to peak brightness");

    const float previousLuminance = luminance;
    expect(!envelope.trigger(retriggerAt - 0.001) &&
               !envelope.valueAt(retriggerAt - 0.001, luminance) &&
               luminance == previousLuminance,
           "out-of-order timestamps are rejected without changing output state");
    expect(!envelope.trigger(std::numeric_limits<double>::quiet_NaN()) &&
               !envelope.valueAt(std::numeric_limits<double>::infinity(),
                                 luminance),
           "non-finite timestamps are rejected");
}

void testStepTriggerEventQueue() {
    using namespace pocket::ui;

    StepTriggerEventQueue queue;
    StepTriggerEvent event;
    expect(!queue.pop(event), "step-trigger queue starts empty");

    pocket::TickResult first;
    first.masterTickPosition = 7;
    first.stepPositions[0] = 3;
    first.triggered[0] = true;
    expect(queue.pushTick(48000, first),
           "queue accepts a timestamped step-trigger tick");

    pocket::TickResult second;
    second.masterTickPosition = 8;
    second.stepPositions[0] = 4;
    second.triggered[0] = true;
    second.triggered[2] = true;
    expect(queue.pushTick(54000, second),
           "queue accepts consecutive trigger ticks");

    StepTriggerEvent received;
    expect(queue.pop(received) &&
               received.audioFrame == 48000 &&
               received.masterTickPosition == first.masterTickPosition &&
               received.stepPositions[0] == first.stepPositions[0] &&
               received.triggeredMask == 0x01,
           "queue preserves the first event payload and order");
    expect(queue.pop(received) &&
               received.audioFrame == 54000 &&
               received.masterTickPosition == second.masterTickPosition &&
               received.triggeredMask == 0x05,
           "queue preserves the next event payload and order");

    pocket::TickResult noTriggers;
    expect(!queue.pushTick(60000, noTriggers) && queue.droppedCount() == 0,
           "empty or invalid trigger ticks do not consume queue capacity");

    for (std::uint32_t i = 0; i < StepTriggerEventQueue::Capacity; ++i) {
        first.masterTickPosition = i;
        expect(queue.pushTick(i, first),
               "queue accepts events up to its fixed capacity");
    }
    expect(!queue.pushTick(100, second) && queue.droppedCount() == 1,
           "full queue drops the newest event and records the overrun");
    for (std::uint32_t i = 0; i < StepTriggerEventQueue::Capacity; ++i) {
        expect(queue.pop(received) && received.audioFrame == i,
               "full queue drains its retained events in order");
    }
    expect(!queue.pop(received), "queue is empty after draining");
    queue.reset();
    expect(queue.droppedCount() == 0 && !queue.pop(received),
           "queue reset clears pending events and the overrun counter");
}

void testStepLightBank() {
    using namespace pocket::ui;
    constexpr std::uint32_t sampleRate = 48000;

    StepTriggerEventQueue queue;
    StepLightBank lights;
    float luminance = -1.0F;

    pocket::TickResult first;
    first.stepPositions[0] = 3;
    first.triggered[0] = true;
    expect(queue.pushTick(4800, first) &&
               lights.consume(queue, 4800, sampleRate) &&
               lights.valueAt(0, 3, 4800 + 865, sampleRate, luminance) &&
               luminance == 1.0F,
           "audio-frame event drives the matching step illumination envelope");

    pocket::TickResult future;
    future.stepPositions[0] = 4;
    future.triggered[0] = true;
    expect(queue.pushTick(6000, future) &&
               lights.consume(queue, 5500, sampleRate) &&
               lights.valueAt(0, 4, 5500, sampleRate, luminance) &&
               luminance == 0.0F,
           "future audio events remain pending until their frame is reached");
    expect(lights.consume(queue, 6000, sampleRate) &&
    lights.valueAt(0, 4, 6865, sampleRate, luminance) &&
               luminance == 1.0F,
           "pending event triggers when the audio timeline reaches its frame");

    pocket::TickResult late;
    late.stepPositions[0] = 5;
    late.triggered[0] = true;
    expect(queue.pushTick(1000, late) &&
               lights.consume(queue, 60000, sampleRate) &&
               lights.staleEventCount() == 1 &&
               lights.valueAt(0, 5, 60000, sampleRate, luminance) &&
               luminance == 0.0F,
           "events older than the light envelope are discarded, not replayed");

    expect(!lights.consume(queue, 60000, 0) &&
               !lights.valueAt(0, 3, 60000, 0, luminance),
           "step-light timing rejects a zero sample rate");
    expect(!lights.valueAt(pocket::TrackCount, 0, 60000, sampleRate,
                           luminance) &&
               !lights.valueAt(
                   0, pocket::EuclideanGenerator::MaxLength, 60000,
                               sampleRate, luminance),
           "step-light lookup rejects out-of-range track and step indices");

    lights.reset();
    expect(lights.staleEventCount() == 0 &&
               lights.valueAt(0, 3, 60000, sampleRate, luminance) &&
               luminance == 0.0F,
           "reset clears pending events, stale count, and all illumination");
}

void testThemeCatalogCycling() {
    using namespace pocket;

    bool forwardValid = ui::themeCount() >= 8;
    bool backwardValid = forwardValid;
    for (std::size_t index = 0; index < ui::themeCount(); ++index) {
        const auto* current = ui::themeAt(index);
        const auto* next = ui::themeAt((index + 1) % ui::themeCount());
        const auto* previous =
            ui::themeAt((index + ui::themeCount() - 1) % ui::themeCount());
        forwardValid = forwardValid && current != nullptr && next != nullptr &&
                       ui::adjacentTheme(current->id, 1) == next->id;
        backwardValid =
            backwardValid && current != nullptr && previous != nullptr &&
            ui::adjacentTheme(current->id, -1) == previous->id;
    }
    expect(forwardValid, "theme catalog cycles forward in display order");
    expect(backwardValid, "theme catalog cycles backward in display order");
    expect(!ui::adjacentTheme(platform::ThemeId::QuietDark, 0).has_value() &&
               !ui::adjacentTheme(static_cast<platform::ThemeId>(255), 1)
                    .has_value(),
           "theme catalog rejects zero direction and unknown theme identifiers");
}

void testMainScreenSnapshot() {
    using namespace pocket;

    Sequencer sequencer;
    expect(sequencer.setTempo(96.0), "configure main screen tempo");
    expect(sequencer.setRhythm(0, {7, 3, 2}),
           "configure a non-default main screen pattern");
    expect(sequencer.setMuted(1, true), "mute a main screen track");
    expect(sequencer.setInstrument(2, InstrumentType::Synth),
           "assign a main screen synth track");
    sequencer.start();
    const auto tick = sequencer.tick();

    ui::MainScreenSnapshot snapshot;
    expect(ui::makeMainScreenSnapshot(sequencer, 2, snapshot),
           "main screen creates a fixed-size snapshot for all tracks");
    expect(snapshot.tempoBpm == 96.0 && snapshot.playing &&
               snapshot.masterTickPosition == sequencer.masterTickPosition(),
           "main screen snapshot contains current transport status");
    expect(snapshot.tracks[0].rhythm.length == 7 &&
               snapshot.tracks[0].rhythm.fill == 3 &&
               snapshot.tracks[0].rhythm.offset == 2 &&
               snapshot.tracks[0].instrument == InstrumentType::Sampler,
           "main screen snapshot reflects effective rhythm and instrument state");
    std::size_t activeCount = 0;
    for (std::uint8_t step = 0; step < snapshot.tracks[0].rhythm.length; ++step) {
        activeCount += snapshot.tracks[0].activeSteps[step] ? 1U : 0U;
    }
    expect(activeCount == 3,
           "main screen pattern marks exactly the configured active steps");
    expect(snapshot.tracks[0].selected == false &&
               snapshot.tracks[1].muted &&
               snapshot.tracks[2].selected &&
               snapshot.tracks[2].instrument == InstrumentType::Synth,
           "main screen rows expose selection, mute, and instrument state");
    expect(snapshot.tracks[0].position == sequencer.track(0).position,
           "main screen position follows the sequencer's next-step position");

    const auto oldTempo = snapshot.tempoBpm;
    expect(!ui::makeMainScreenSnapshot(sequencer, TrackCount, snapshot) &&
               snapshot.tempoBpm == oldTempo,
           "invalid selected track is rejected without partially changing the snapshot");
}

void testLiveModulationOverlay() {
    using namespace pocket;

    ui::ModulationMirror mirror;
    expect(mirror.isLockFree(), "modulation mirror uses lock-free atomics");
    expect(!mirror.read(0).valid,
           "modulation mirror reports no data before the first publish");

    mirror.publish(0, {12, 5, 3}, 0.25F, -0.5F);
    const auto live = mirror.read(0);
    expect(live.valid && live.rhythm.length == 12 && live.rhythm.fill == 5 &&
               live.rhythm.offset == 3 && live.lfoPhase == 0.25F &&
               live.lfoValue == -0.5F,
           "modulation mirror round-trips rhythm and LFO state");

    mirror.publishJitter(0, 0x0AAAU, true);
    const auto liveJitter = mirror.read(0);
    expect(liveJitter.jitterValid && liveJitter.jitterEvents == 0x0AAAU,
           "modulation mirror round-trips the post-jitter trigger mask");
    mirror.publishJitter(0, 0, false);

    mirror.publish(ui::ModulationMirror::Tracks, {8, 3, 0}, 1.0F, 1.0F);
    expect(!mirror.read(ui::ModulationMirror::Tracks).valid,
           "modulation mirror ignores out-of-range tracks");

    Sequencer sequencer;
    expect(sequencer.setRhythm(0, {16, 4, 0}),
           "configure base rhythm for the live overlay");
    sequencer.start();
    (void)sequencer.tick();

    std::array<ui::TrackAudioSettings, TrackCount> audio{};
    ui::MainScreenSnapshot snapshot;
    ui::MainScreenLiveModulation overlay{&mirror, true};
    expect(ui::makeMainScreenSnapshot(sequencer, 0, audio, overlay, snapshot) &&
               snapshot.tracks[0].rhythm.length == 12 &&
               snapshot.tracks[0].rhythm.fill == 5 &&
               snapshot.tracks[0].rhythm.offset == 3,
           "live overlay replaces the base rhythm with the audio-thread value");
    std::size_t activeCount = 0;
    for (std::uint8_t step = 0; step < snapshot.tracks[0].rhythm.length;
         ++step) {
        activeCount += snapshot.tracks[0].activeSteps[step] ? 1U : 0U;
    }
    expect(activeCount == 5,
           "live overlay recomputes active steps for the modulated rhythm");

    mirror.publishJitter(0, 0x0AAAU, true);
    expect(ui::makeMainScreenSnapshot(sequencer, 0, audio, overlay, snapshot) &&
               !snapshot.tracks[0].activeSteps[0] &&
               snapshot.tracks[0].activeSteps[1] &&
               !snapshot.tracks[0].activeSteps[2] &&
               snapshot.tracks[0].activeSteps[3],
           "live overlay applies the post-jitter trigger mask");
    mirror.publishJitter(0, 0, false);

    overlay.active = false;
    expect(ui::makeMainScreenSnapshot(sequencer, 0, audio, overlay, snapshot) &&
               snapshot.tracks[0].rhythm.length == 16 &&
               snapshot.tracks[0].rhythm.fill == 4,
           "inactive overlay falls back to the UI-side rhythm");

    mirror.reset();
    expect(!mirror.read(0).valid,
           "resetting the mirror clears the published state");
}

void testJitterOverlayFallback() {
    using namespace pocket;

    Sequencer sequencer;
    expect(sequencer.setRhythm(0, {8, 4, 0}),
           "configure rhythm for the UI-side jitter overlay");
    JitterConfig jitter;
    jitter.enabled = true;
    jitter.mode = JitterMode::Drop;
    jitter.intensity = 100;
    jitter.rate = JitterRate::Quarter;
    expect(sequencer.setJitter(0, jitter),
           "configure jitter for the UI-side overlay");
    sequencer.start();
    (void)sequencer.tick();

    bool jitterValid = false;
    expect(sequencer.jitterEventsMask(0, jitterValid) == 0 && jitterValid,
           "sequencer exposes the all-dropped mask after its own tick");

    ui::MainScreenSnapshot snapshot;
    expect(ui::makeMainScreenSnapshot(sequencer, 0, snapshot),
           "main snapshot builds with the UI-side jitter mask");
    std::size_t activeCount = 0;
    for (std::uint8_t step = 0; step < snapshot.tracks[0].rhythm.length;
         ++step) {
        activeCount += snapshot.tracks[0].activeSteps[step] ? 1U : 0U;
    }
    expect(activeCount == 0,
           "UI-side jitter mask removes dropped steps from the snapshot");

    jitter.enabled = false;
    expect(sequencer.setJitter(0, jitter),
           "disable jitter after the overlay check");
    expect(sequencer.jitterEventsMask(0, jitterValid) == 0 && !jitterValid,
           "disabled jitter reports no live display mask");
}

void testMainScreenLayout() {
    using namespace pocket;

    Sequencer sequencer;
    expect(sequencer.setRhythm(0, {32, 7, 9}),
           "configure maximum-length pattern for layout");
    sequencer.start();
    ui::MainScreenSnapshot snapshot;
    expect(ui::makeMainScreenSnapshot(sequencer, 0, snapshot),
           "create snapshot for main screen layout");

    ui::MainScreenFrame frame;
    expect(ui::layoutMainScreen(snapshot, platform::ThemeId::QuietDark,
                                1024, 768, frame),
           "layout main screen at the Brick Pro design resolution");
    expect(frame.width == 1024 && frame.height == 768 &&
               frame.tracks.size() == TrackCount &&
               frame.tracks[0].stepCount == 32 &&
               frame.tracks[0].trackNumber == 1 &&
               frame.tracks[7].trackNumber == 8,
           "layout contains eight rows and all maximum-length steps");
    expect(frame.rowPitch > 0 && frame.stepPitch > 0 &&
               frame.patternX > frame.tempoX,
           "layout reserves ordered header, row, and pattern regions");

    for (const auto& row : frame.tracks) {
        expect(row.y >= 0 && row.y + row.height <= frame.height,
               "track row remains within the viewport");
        const auto expectedPitch =
            (frame.width - frame.tempoX - frame.patternX) /
            static_cast<std::int32_t>(row.stepCount);
        for (std::uint8_t step = 0; step < row.stepCount; ++step) {
            const auto& bar = row.steps[step];
            expect(bar.x >= frame.patternX &&
                       bar.x + bar.width <= frame.width &&
                       bar.y >= row.y && bar.y + bar.height <= row.y + row.height,
                   "step bars remain inside the viewport and their track row");
            if (step + 1 < row.stepCount) {
                const auto actualPitch = row.steps[step + 1].x - bar.x;
                expect(std::abs(actualPitch - expectedPitch) <= 1 &&
                           bar.x + bar.width <= row.steps[step + 1].x,
                       "elastic step spacing scales uniformly to fill full width for each track length");
            }
        }
        for (std::uint8_t step = 0; step < row.stepCount; ++step) {
            expect(row.steps[step].y == row.steps[0].y &&
                       row.steps[step].height == row.steps[0].height,
                   "active and rest steps share identical vertical y and height in the geometric grid");
        }
        const auto& lastBar = row.steps[row.stepCount - 1];
        expect(row.steps[0].x == frame.patternX &&
                   lastBar.x + lastBar.width == frame.width - frame.tempoX,
               "elastic step bars span the full available pattern width regardless of step count");
    }
    expect(frame.tracks[0].steps[0].next &&
               frame.tracks[0].steps[0].active,
           "playing layout marks the next active step without changing the pattern");

    snapshot.playing = false;
    expect(ui::layoutMainScreen(snapshot, platform::ThemeId::WarmPaper,
                                640, 480, frame) &&
               !frame.tracks[0].steps[0].next &&
               frame.palette.background.red == 244,
           "layout supports desktop-sized viewports, stopped transport, and theme colors");

    const auto previousWidth = frame.width;
    expect(!ui::layoutMainScreen(snapshot, static_cast<platform::ThemeId>(255),
                                 1024, 768, frame) &&
               frame.width == previousWidth,
           "invalid theme is rejected without partially replacing layout");
    expect(!ui::layoutMainScreen(snapshot, platform::ThemeId::QuietDark,
                                 319, 768, frame) &&
               frame.width == previousWidth,
           "unsupported viewport size is rejected without changing the last frame");
}

void testMainScreenDrawList() {
    using namespace pocket;

    Sequencer sequencer;
    expect(sequencer.setRhythm(0, {5, 2, 0}),
           "configure rhythm for main screen drawing");
    expect(sequencer.setMuted(1, true), "mute a track for main screen drawing");
    sequencer.start();

    ui::MainScreenSnapshot snapshot;
    expect(ui::makeMainScreenSnapshot(sequencer, 0, snapshot),
           "create snapshot for main screen draw list");
    ui::MainScreenFrame frame;
    expect(ui::layoutMainScreen(snapshot, platform::ThemeId::QuietDark,
                                1024, 768, frame),
           "create layout for main screen draw list");

    ui::MainScreenDrawList drawList;
    expect(ui::buildMainScreenDrawList(frame, drawList),
           "build fixed-capacity draw list for main screen");
    expect(drawList.commandCount == 3 + 8 + 5 + 7 * 16,
           "draw list contains background, header, row labels, and visible steps only");
    expect(drawList.commands[0].type == ui::MainDrawCommandType::Clear &&
               drawList.commands[0].color.red == frame.palette.background.red,
           "draw list begins with the selected theme background");
    expect(drawList.commands[1].type == ui::MainDrawCommandType::Text &&
               drawList.commands[1].textRole == ui::MainTextRole::Tempo &&
               drawList.commands[1].numericValue == sequencer.tempo(),
           "draw list exposes tempo as semantic text data");
    expect(drawList.commands[2].textRole == ui::MainTextRole::Transport &&
               drawList.commands[2].masterTickPosition ==
                   sequencer.masterTickPosition(),
           "draw list preserves the exact integer transport position");

    bool foundSelectedTrack = false;
    bool foundMutedTrack = false;
    bool foundActiveBar = false;
    bool foundInactiveBar = false;
    for (std::size_t index = 0; index < drawList.commandCount; ++index) {
        const auto& command = drawList.commands[index];
        if (command.type == ui::MainDrawCommandType::Text &&
            command.textRole == ui::MainTextRole::Track) {
            foundSelectedTrack =
                foundSelectedTrack ||
                (command.trackNumber == 1 && command.selected &&
                 command.color.red == frame.palette.accent.red);
            foundMutedTrack =
                foundMutedTrack || (command.trackNumber == 2 && command.muted);
        } else if (command.type == ui::MainDrawCommandType::StepBar) {
            foundActiveBar = foundActiveBar || command.active;
            foundInactiveBar = foundInactiveBar || !command.active;
        }
        if (command.type != ui::MainDrawCommandType::Clear) {
            expect(command.x >= 0 && command.y >= 0 &&
                       command.x + command.width <= frame.width &&
                       command.y + command.height <= frame.height,
                   "every emitted draw command is bounded by the viewport");
        }
    }
    expect(foundSelectedTrack && foundMutedTrack && foundActiveBar &&
               foundInactiveBar,
           "draw list carries selected, muted, active, and inactive states");

    const auto oldCount = drawList.commandCount;
    const auto oldColor = drawList.commands[0].color.red;
    frame.tracks[0].steps[0].width = 0;
    expect(!ui::buildMainScreenDrawList(frame, drawList) &&
               drawList.commandCount == oldCount &&
               drawList.commands[0].color.red == oldColor,
           "invalid geometry is rejected without partially replacing the draw list");

    expect(ui::layoutMainScreen(snapshot, platform::ThemeId::QuietDark,
                                1024, 768, frame),
           "restore valid layout after rejected draw command");
    frame.tracks[1].trackNumber = frame.tracks[0].trackNumber;
    expect(!ui::buildMainScreenDrawList(frame, drawList) &&
               drawList.commandCount == oldCount,
           "duplicate track identifiers are rejected without changing the draw list");

    for (std::size_t track = 0; track < TrackCount; ++track) {
        expect(sequencer.setRhythm(track, {32, 16, 0}),
               "configure every track at maximum pattern length");
    }
    expect(ui::makeMainScreenSnapshot(sequencer, 0, snapshot) &&
               ui::layoutMainScreen(snapshot, platform::ThemeId::QuietDark,
                                    1024, 768, frame) &&
               ui::buildMainScreenDrawList(frame, drawList) &&
               drawList.commandCount == ui::MainScreenMaxDrawCommands,
           "maximum main-screen workload fits the fixed command budget");
}

void testTrackNavigationAndTransport() {
    using namespace pocket;
    using namespace pocket::ui;

    Sequencer sequencer;
    UiController ui(sequencer);
    ui.dispatch({Action::TrackPrevious});
    expect(ui.state().selectedTrack == TrackCount - 1,
           "track navigation wraps from first to last");
    ui.dispatch({Action::NavDown});
    expect(ui.state().selectedTrack == 0, "track navigation wraps to first");
    ui.dispatch({Action::PlayStop});
    expect(sequencer.isPlaying(), "transport starts");
    ui.dispatch({Action::PlayStop});
    expect(!sequencer.isPlaying(), "transport stops");
    ui.dispatch({Action::MuteToggle});
    expect(sequencer.track(0).muted, "selected track mute toggles");
}

void testSynthEngineSelection() {
    using namespace pocket;
    using namespace pocket::ui;

    Sequencer sequencer;
    sequencer.setInstrument(0, InstrumentType::Synth);
    audio::AudioParameterQueue queue;
    UiController ui(sequencer);
    ui.setAudioParameterQueue(&queue);
    ui.dispatch({Action::ScreenNext}); // Edit
    ui.dispatch({Action::ScreenNext}); // Sound
    for (int i = 0; i < 4; ++i) {
        ui.dispatch({Action::NavDown});
    }
    expect(ui.state().soundField == SoundField::EngineType,
           "SOUND navigation reaches the engine type field");
    expect(ui.effectiveTrackState(0).synth.engineType ==
               SynthEngineType::BasicVa,
           "engine field starts on BASIC-VA");

    ui.dispatch({Action::NavRight, false, true});
    expect(ui.effectiveTrackState(0).synth.engineType ==
               SynthEngineType::Plaits,
           "right with the edit modifier selects the Plaits engine");
    audio::AudioParameterCommand command;
    expect(queue.pop(command) &&
               command.parameter ==
                   audio::AudioParameter::TrackSynthEngineType &&
               command.track == 0 && command.value == 1.0F,
           "engine change is published through the audio parameter queue");

    ui.dispatch({Action::Confirm});
    expect(ui.effectiveTrackState(0).synth.engineType ==
               SynthEngineType::BasicVa,
           "confirm toggles the discrete engine field back to BASIC-VA");

    ui.dispatch({Action::NavDown});
    expect(ui.state().soundField == SoundField::Model,
           "SOUND navigation reaches the Plaits model field");
    ui.dispatch({Action::NavRight, false, true});
    expect(ui.effectiveTrackState(0).synth.model == 1,
           "right with the edit modifier selects the next Plaits model");
    audio::AudioParameterCommand modelCommand;
    while (queue.pop(modelCommand) &&
           modelCommand.parameter != audio::AudioParameter::TrackSynthModel) {
    }
    expect(modelCommand.parameter == audio::AudioParameter::TrackSynthModel &&
               modelCommand.track == 0 && modelCommand.value == 1.0F,
           "model change is published through the audio parameter queue");
    ui.dispatch({Action::NavLeft, false, true});
    ui.dispatch({Action::NavLeft, false, true});
    expect(ui.effectiveTrackState(0).synth.model == 15,
           "model field wraps from zero to the last model");

    sequencer.setInstrument(0, InstrumentType::Sampler);
    ui.dispatch({Action::NavUp});
    ui.dispatch({Action::Confirm});
    expect(ui.effectiveTrackState(0).synth.model == 15,
           "confirm on EngineType is ignored while track instrument is Sampler");

    sequencer.setInstrument(0, InstrumentType::Synth);
    for (int i = 0; i < 6; ++i) {
        ui.dispatch({Action::NavDown});
    }
    expect(ui.state().soundField == SoundField::Sample,
           "navigation reaches sample row");
    ui.dispatch({Action::Confirm});
    expect(ui.state().modal == Modal::None,
           "confirm on Sample does not open file selector while track instrument is Synth");
}

void testSampleBrowser() {
    using namespace pocket;
    using namespace pocket::ui;

    Sequencer sequencer;
    UiController ui(sequencer);
    ui.dispatch({Action::ScreenNext}); // Edit
    ui.dispatch({Action::ScreenNext}); // Sound
    for (int i = 0; i < 10; ++i) {
        ui.dispatch({Action::NavDown});
    }
    expect(ui.state().soundField == SoundField::Sample,
           "SOUND navigation reaches the sample field");

    ui.dispatch({Action::Confirm});
    expect(ui.state().modal == Modal::FileSelector &&
               ui.fileBrowser().active,
           "confirm on SAMPLE opens the WAV browser");
    expect(ui.takeFileBrowserRequest() == FileBrowserRequest::Scan,
           "opening the browser asks the app layer for a directory listing");

    ui.setFileBrowserEntries(
        "C:/wav",
        {{"..", "C:/", "<DIR>", true},
         {"kick.wav", "C:/wav/kick.wav", "WAV", false},
         {"snare.wav", "C:/wav/snare.wav", "WAV", false}});
    ui.dispatch({Action::NavUp});
    expect(ui.fileBrowser().cursor == 0,
           "file cursor clamps at the first entry");
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavDown});
    expect(ui.fileBrowser().cursor == 2,
           "file cursor clamps at the last entry");

    ui.dispatch({Action::Confirm});
    std::string chosenPath;
    std::size_t chosenTrack = 99;
    expect(ui.state().modal == Modal::None &&
               ui.takeChosenSample(chosenPath, chosenTrack) &&
               chosenPath == "C:/wav/snare.wav" && chosenTrack == 0,
           "confirm on a WAV closes the browser and reports the track and path");

    ui.dispatch({Action::Confirm});
    ui.dispatch({Action::NavUp});
    ui.dispatch({Action::Confirm});
    expect(ui.takeFileBrowserRequest() == FileBrowserRequest::Scan &&
               ui.fileBrowser().directory == "C:/",
           "confirm on a directory asks the app layer to rescan it");
    ui.dispatch({Action::Back});
    expect(ui.state().modal == Modal::None && !ui.fileBrowser().active,
           "back closes the WAV browser");
}

void testUiAudioParameterQueue() {
    using namespace pocket;
    using namespace pocket::ui;

    Sequencer sequencer;
    expect(sequencer.setVolume(0, 50), "set initial queued mixer volume");
    UiController ui(sequencer);
    audio::AudioParameterQueue queue;
    ui.setAudioParameterQueue(&queue);

    ui.dispatch({Action::ScreenNext});
    ui.dispatch({Action::ScreenNext});
    const float initialAttack =
        ui.state().audioTrackSettings[0].envelope.attackSeconds;
    ui.dispatch({Action::NavRight, false, true});
    audio::AudioParameterCommand command;
    expect(queue.pop(command) &&
               command.parameter == audio::AudioParameter::TrackAttack &&
               command.track == 0 &&
               command.value > initialAttack &&
               command.value ==
                   ui.state().audioTrackSettings[0].envelope.attackSeconds,
           "sound edits publish the UI-owned attack value through the audio queue");

    for (std::size_t index = 0; index < audio::AudioParameterQueue::Capacity;
         ++index) {
        expect(queue.push(
                   {audio::AudioParameter::TrackMute, 0, 0.0F}),
               "fill bounded audio parameter queue for rejection test");
    }
    const float oldDecay = ui.state().audioTrackSettings[0].envelope.decaySeconds;
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavRight, false, true});
    expect(ui.state().audioParameterRejected &&
               ui.state().audioTrackSettings[0].envelope.decaySeconds ==
                   oldDecay,
           "full audio queue rejects the edit without changing UI audio state");
    ui.dispatch({Action::NavUp});
    expect(!ui.state().audioParameterRejected,
           "the next input clears the transient audio queue error state");

    for (std::size_t index = 0; index < audio::AudioParameterQueue::Capacity;
         ++index) {
        expect(queue.pop(command), "drain queue after rejection test");
    }
    ui.dispatch({Action::ScreenNext});
    ui.dispatch({Action::NavRight, false, true});
    expect(queue.pop(command) &&
               command.parameter == audio::AudioParameter::TrackVolume &&
               command.value == 55.0F &&
               ui.state().audioTrackSettings[0].volume == 55 &&
               sequencer.track(0).volume == 50,
           "mixer edits keep UI state separate until the audio thread applies them");
    ui.dispatch({Action::MuteToggle});
    expect(queue.pop(command) &&
               command.parameter == audio::AudioParameter::TrackMute &&
               command.value == 1.0F &&
               ui.state().audioTrackSettings[0].muted &&
               !sequencer.track(0).muted,
           "live mute is queued without writing the audio-owned sequencer state");
}

void testScreensAndEditRanges() {
    using namespace pocket;
    using namespace pocket::ui;

    Sequencer sequencer;
    UiController ui(sequencer);
    ui.dispatch({Action::ScreenNext});
    expect(ui.state().screen == Screen::Edit, "main advances to edit");
    ui.dispatch({Action::ScreenNext});
    expect(ui.state().screen == Screen::Sound, "edit advances to sound");
    ui.dispatch({Action::NavRight, false, true});
    expect(sequencer.track(0).envelope.attackSeconds > 0.005F &&
               ui.state().modified,
           "sound screen adjusts the selected track attack envelope");
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavRight, true, true});
    expect(sequencer.track(0).envelope.decaySeconds > 0.45F &&
               ui.state().soundField == SoundField::Decay,
           "sound screen fine-adjusts the selected track decay envelope");
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavRight, false, true});
    expect(sequencer.track(0).filter.enabled &&
               ui.state().soundField == SoundField::FilterEnabled,
           "sound screen enables the selected track low-pass filter");
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavRight, true, true});
    expect(sequencer.track(0).filter.cutoffHz == 12100.0F &&
               ui.state().soundField == SoundField::FilterCutoff,
           "sound screen fine-adjusts the selected track cutoff");
    ui.dispatch({Action::ScreenNext});
    expect(ui.state().screen == Screen::Mixer,
           "sound advances to mixer screen");
    ui.dispatch({Action::ScreenNext});
    expect(ui.state().screen == Screen::Modulation,
           "mixer advances to modulation screen");
    ui.dispatch({Action::ScreenNext});
    expect(ui.state().screen == Screen::Turing,
           "modulation advances to the Turing screen");
    ui.dispatch({Action::ScreenNext});
    expect(ui.state().screen == Screen::System, "Turing advances to system screen");
    ui.dispatch({Action::ScreenNext});
    expect(ui.state().screen == Screen::Main, "all primary screens wrap around");

    ui.dispatch({Action::ScreenNext});
    ui.dispatch({Action::ScreenNext});
    ui.dispatch({Action::ScreenNext});
    expect(ui.state().screen == Screen::Mixer,
           "mixer is directly reachable by cycling from the main screen");
    // Mixer columns move with left/right; values need A (or Ctrl) held, the
    // same tracker-style modifier as SOUND, MODULATION, and TURING.
    ui.dispatch({Action::NavRight});
    expect(ui.state().mixerField == MixerField::Pan &&
               sequencer.track(0).volume == 100,
           "mixer left/right moves the column without changing the value");
    ui.dispatch({Action::NavLeft});
    expect(ui.state().mixerField == MixerField::Volume,
           "mixer left moves back to volume column");
    ui.dispatch({Action::NavLeft, false, true});
    expect(sequencer.track(0).volume == 95 &&
               ui.state().mixerField == MixerField::Volume,
           "mixer adjusts the selected track volume with A plus left/right");
    ui.dispatch({Action::NavRight, false, true});
    expect(sequencer.track(0).volume == 100,
           "mixer volume remains within its 0 to 100 percent range");
    ui.dispatch({Action::NavLeft, false, true});
    ui.dispatch({Action::NavRight});
    ui.dispatch({Action::NavLeft, false, true});
    expect(sequencer.track(0).pan < 0.0F &&
               ui.state().mixerField == MixerField::Pan,
           "mixer adjusts normalized track pan");
    ui.dispatch({Action::MuteToggle});
    expect(sequencer.track(0).muted,
           "mixer mute shortcut suppresses the selected track");
    const auto mixerSettings = ui.systemSettings().audio.masterVolumePercent;
    ui.dispatch({Action::NavRight});
    expect(ui.state().mixerField == MixerField::Mute,
           "mixer column order reaches mute after pan");
    ui.dispatch({Action::Confirm});
    expect(!sequencer.track(0).muted,
           "mixer A tap toggles the mute switch");
    ui.dispatch({Action::NavRight});
    expect(ui.state().mixerField == MixerField::Lock,
           "mixer column order reaches the phrase lock after mute");
    ui.dispatch({Action::Confirm});
    expect(sequencer.track(0).turing.locked,
           "mixer lock freezes the selected track phrase");
    ui.dispatch({Action::NavRight});
    expect(ui.state().mixerField == MixerField::Dice,
           "mixer column order reaches the dice after lock");
    const auto seedBeforeDice = sequencer.track(0).turing.randomSeed;
    ui.dispatch({Action::Confirm});
    expect(sequencer.track(0).turing.randomSeed != seedBeforeDice &&
               sequencer.track(0).turing.useCustomSeed,
           "mixer dice rolls a new phrase seed");
    ui.dispatch({Action::NavRight});
    expect(ui.state().mixerField == MixerField::ReverbSend,
           "mixer column order reaches the reverb send after the dice");
    ui.dispatch({Action::NavRight, false, true});
    expect(sequencer.track(0).reverbSend > 0.0F,
           "mixer adjusts the selected track reverb send");
    ui.dispatch({Action::NavRight});
    ui.dispatch({Action::NavRight, true, true});
    expect(sequencer.track(0).delaySend > 0.0F &&
               ui.state().mixerField == MixerField::DelaySend,
           "mixer adjusts the selected track delay send");
    // Rows walk TRK1..TRK8 and then the master strip, like the parameter
    // screens walk their rows.
    ui.dispatch({Action::NavUp});
    expect(ui.state().mixerMasterRow,
           "mixer up from the first track selects the master strip");
    ui.dispatch({Action::NavLeft, false, true});
    expect(ui.systemSettings().audio.masterVolumePercent < mixerSettings,
           "mixer master volume updates the global audio preference");
    ui.dispatch({Action::NavUp});
    expect(!ui.state().mixerMasterRow &&
               ui.state().selectedTrack == 7,
           "mixer up from the master strip wraps to the last track");
    ui.dispatch({Action::NavDown});
    expect(ui.state().mixerMasterRow,
           "mixer down from the last track reaches the master strip");
    ui.dispatch({Action::NavDown});
    expect(!ui.state().mixerMasterRow &&
               ui.state().selectedTrack == 0,
           "mixer down from the master strip wraps to the first track");
    ui.dispatch({Action::NavUp});
    expect(ui.state().mixerMasterRow,
           "mixer up from the first track reaches the master strip");

    ui.dispatch({Action::Back});
    ui.dispatch({Action::Confirm});
    expect(ui.state().screen == Screen::Edit, "confirm opens edit from main");
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavRight});
    expect(sequencer.track(0).rhythm.offset == 1, "edit changes pattern offset");

    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavRight});
    expect(sequencer.track(0).instrument == InstrumentType::Synth,
           "edit changes instrument assignment");
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavLeft});
    ui.dispatch({Action::NavRight, true});
    expect(sequencer.track(0).volume == 91,
           "fine adjustment changes volume by one after coarse adjustment");

    ui.dispatch({Action::Back});
    expect(ui.state().screen == Screen::Main, "back returns from edit to main");
    ui.dispatch({Action::TrackNext});
    expect(ui.state().selectedTrack == 1,
           "selected track remains globally accessible across screens");
    ui.dispatch({Action::ScreenNext});
    ui.dispatch({Action::TrackNext});
    expect(ui.state().selectedTrack == 2,
           "track selection remains available on non-main screens");

    Sequencer clampSequencer;
    clampSequencer.setRhythm(0, {3, 3, 0});
    UiController clampUi(clampSequencer);
    clampUi.dispatch({Action::Confirm});
    clampUi.dispatch({Action::NavLeft});
    expect(clampSequencer.track(0).rhythm.length == 2 &&
               clampSequencer.track(0).rhythm.fill == 2,
           "reducing length clamps fill to the new length");

    Sequencer modulationSequencer;
    const platform::SystemCapabilities desktopCapabilities{
        true, true, true, true, true, true};
    UiController modulationUi(modulationSequencer, desktopCapabilities);
    modulationUi.dispatch({Action::ScreenNext});
    modulationUi.dispatch({Action::ScreenNext});
    modulationUi.dispatch({Action::ScreenNext});
    modulationUi.dispatch({Action::ScreenNext});
    expect(modulationUi.state().screen == Screen::Modulation,
           "modulation screen is reachable in the primary screen sequence");
    modulationUi.dispatch({Action::NavDown});
    modulationUi.dispatch({Action::NavRight, false, true});
    expect(modulationSequencer.track(0).lfos[0].waveform == LfoWaveform::Triangle,
           "modulation screen can change the selected track LFO waveform");
    modulationUi.dispatch({Action::NavDown});
    modulationUi.dispatch({Action::NavDown});
    modulationUi.dispatch({Action::NavDown});
    modulationUi.dispatch({Action::NavRight, false, true});
    expect(modulationSequencer.track(0).lfos[0].destination ==
               ModulationDestination::Fill,
           "modulation screen selects one parameter destination for its LFO");
    modulationUi.dispatch({Action::NavDown});
    modulationUi.dispatch({Action::NavDown});
    modulationUi.dispatch({Action::Confirm});
    expect(modulationSequencer.track(0).jitter.enabled,
           "modulation screen exposes an accessible per-track jitter enable");
    modulationUi.dispatch({Action::NavDown});
    modulationUi.dispatch({Action::NavRight, false, true});
    expect(modulationSequencer.track(0).jitter.intensity == 5,
           "modulation screen exposes per-track jitter intensity");
    modulationUi.dispatch({Action::ScreenNext});
    expect(modulationUi.state().screen == Screen::Turing,
           "Turing screen follows modulation screen");
    modulationUi.dispatch({Action::ScreenNext});
    expect(modulationUi.state().screen == Screen::System,
           "system screen follows Turing screen");
    modulationUi.dispatch({Action::NavDown});
    expect(modulationUi.state().systemCategory ==
               platform::SettingsCategory::Midi,
           "system screen navigates categorized settings");
    expect(!modulationUi.systemSettings().midi.enabled,
           "MIDI input defaults to disabled");
    const bool dirtyBeforeMidi = modulationUi.state().modified;
    modulationUi.dispatch({Action::NavRight});
    expect(modulationUi.systemSettings().midi.enabled &&
               modulationUi.state().modified == dirtyBeforeMidi,
           "MIDI category enables input without changing project dirty state");
    modulationUi.dispatch({Action::NavLeft});
    expect(!modulationUi.systemSettings().midi.enabled,
           "MIDI category disables input on left");
    expect(platform::isValid(modulationUi.systemSettings()),
           "UI exposes a valid separate system settings model");

    Sequencer turingSequencer;
    UiController turingUi(turingSequencer);
    for (int screen = 0; screen < 5; ++screen) {
        turingUi.dispatch({Action::ScreenNext});
    }
    expect(turingUi.state().screen == Screen::Turing &&
               turingUi.state().turingField == TuringField::Enabled,
           "Turing screen is reachable from the main screen");
    turingUi.dispatch({Action::NavRight, false, true});
    expect(turingSequencer.track(0).turing.enabled,
           "Turing screen enables the selected track generator");
    turingUi.dispatch({Action::NavDown});
    turingUi.dispatch({Action::NavRight, false, true});
    expect(turingSequencer.track(0).turing.locked,
           "Turing screen locks the selected track phrase");
    turingUi.dispatch({Action::NavDown});
    turingUi.dispatch({Action::NavRight, false, true});
    expect(turingSequencer.track(0).turing.clock == TuringClockMode::Euclid,
           "Turing screen selects the Euclidean clock mode");
    turingUi.dispatch({Action::NavDown});
    turingUi.dispatch({Action::NavRight, false, true});
    expect(turingSequencer.track(0).turing.rate == TuringRate::Eighth,
           "Turing screen changes the clock rate");
    turingUi.dispatch({Action::NavDown});
    turingUi.dispatch({Action::NavRight, false, true});
    expect(turingSequencer.track(0).turing.length == 9,
           "Turing screen edits the loop length");
    const auto registerBefore = turingSequencer.turingDisplay(0).values;
    const auto seedBefore = turingSequencer.track(0).turing.randomSeed;
    turingUi.dispatch({Action::Confirm});
    expect(turingSequencer.turingDisplay(0).values != registerBefore &&
               turingSequencer.track(0).turing.randomSeed != seedBefore &&
               turingSequencer.track(0).turing.useCustomSeed &&
               turingUi.state().modified,
           "Confirm throws the phrase dice and records the new seed");
    const auto seedAfterDice = turingSequencer.track(0).turing.randomSeed;
    for (int field = 0; field < 7; ++field) {
        turingUi.dispatch({Action::NavDown});
    }
    expect(turingUi.state().turingField == TuringField::CustomSeed,
           "Turing right column is reachable by vertical navigation");
    turingUi.dispatch({Action::Confirm});
    expect(!turingSequencer.track(0).turing.useCustomSeed,
           "Turing Confirm toggles the custom seed switch off");
    turingUi.dispatch({Action::Confirm});
    turingUi.dispatch({Action::NavDown});
    turingUi.dispatch({Action::NavRight, false, true});
    expect(turingSequencer.track(0).turing.useCustomSeed &&
               turingSequencer.track(0).turing.randomSeed ==
                   std::min<std::uint32_t>(seedAfterDice + 1, 65535),
           "Turing screen edits the custom seed");
    turingUi.dispatch({Action::Back});
    expect(turingUi.state().screen == Screen::Modulation,
           "Turing back returns to the modulation screen");

    Sequencer handheldSequencer;
    const platform::SystemCapabilities handheldCapabilities{
        false, false, true, false, true, true};
    UiController handheldUi(handheldSequencer, handheldCapabilities);
    for (int screen = 0; screen < 6; ++screen) {
        handheldUi.dispatch({Action::ScreenNext});
    }
    handheldUi.dispatch({Action::NavDown});
    expect(handheldUi.state().systemCategory ==
               platform::SettingsCategory::InputAndControls,
           "SYSTEM navigation skips categories unsupported by the current platform");
}

void testParameterColumnNavigation() {
    using namespace pocket;
    using namespace pocket::ui;

    Sequencer sequencer;
    UiController ui(sequencer);
    ui.dispatch({Action::ScreenNext}); // Edit
    ui.dispatch({Action::ScreenNext}); // Sound
    expect(ui.state().soundField == SoundField::Attack,
           "SOUND starts on the first left-column field");
    const float attackBefore =
        ui.state().audioTrackSettings[0].envelope.attackSeconds;

    ui.dispatch({Action::NavRight});
    expect(ui.state().soundField == SoundField::Morph &&
               ui.state().audioTrackSettings[0].envelope.attackSeconds ==
                   attackBefore,
           "plain right jumps to the same row of the right column without editing");
    ui.dispatch({Action::NavLeft});
    expect(ui.state().soundField == SoundField::Attack,
           "plain left returns to the same row of the left column");

    ui.dispatch({Action::NavRight});
    for (int i = 0; i < 6; ++i) {
        ui.dispatch({Action::NavDown});
    }
    expect(ui.state().soundField == SoundField::DelayMix,
           "SOUND right-column navigation reaches the last field");
    ui.dispatch({Action::NavRight});
    expect(ui.state().soundField == SoundField::DelayMix,
           "right at the rightmost column does not wrap or edit");

    ui.dispatch({Action::NavLeft});
    ui.dispatch({Action::NavLeft});
    expect(ui.state().soundField == SoundField::Pitch,
           "plain left returns to the same row of the left column and stops");
    for (int i = 0; i < 6; ++i) {
        ui.dispatch({Action::NavUp});
    }
    expect(ui.state().soundField == SoundField::Attack,
           "vertical navigation returns to the first left-column field");
    ui.dispatch({Action::NavRight, false, true});
    expect(ui.state().soundField == SoundField::Attack &&
               ui.state().audioTrackSettings[0].envelope.attackSeconds >
                   attackBefore,
           "edit+right changes the value and keeps the focused field");
    const float attackAfterCoarse =
        ui.state().audioTrackSettings[0].envelope.attackSeconds;
    ui.dispatch({Action::NavRight, true, true});
    expect(ui.state().soundField == SoundField::Attack &&
               ui.state().audioTrackSettings[0].envelope.attackSeconds >
                   attackAfterCoarse &&
               ui.state().audioTrackSettings[0].envelope.attackSeconds -
                       attackAfterCoarse <
                   0.01F,
           "shift+edit+right applies a fine value step");

    Sequencer modSequencer;
    UiController mod(modSequencer);
    for (int i = 0; i < 4; ++i) {
        mod.dispatch({Action::ScreenNext});
    }
    expect(mod.state().screen == Screen::Modulation,
           "MODULATION is reachable for column navigation");
    for (int i = 0; i < 5; ++i) {
        mod.dispatch({Action::NavDown});
    }
    expect(mod.state().modulationField == ModulationField::Amount,
           "MODULATION column navigation starts from Amount");
    mod.dispatch({Action::NavRight});
    expect(mod.state().modulationField == ModulationField::JitterCustomSeed,
           "plain right moves from the LFO column to the jitter column");
    mod.dispatch({Action::NavLeft});
    expect(mod.state().modulationField == ModulationField::Amount,
           "plain left returns from jitter to the LFO column");
}

void testModalsAndFineMode() {
    using namespace pocket;
    using namespace pocket::ui;

    Sequencer sequencer;
    UiController ui(sequencer);
    ui.dispatch({Action::Help});
    expect(ui.state().modal == Modal::Help, "help opens as a modal");
    ui.dispatch({Action::TrackNext});
    expect(ui.state().selectedTrack == 0,
           "modal blocks ordinary underlying screen actions");
    ui.dispatch({Action::Back});
    expect(ui.state().modal == Modal::None, "back closes help modal");

    ui.dispatch({Action::ProjectMenu});
    expect(ui.state().modal == Modal::ProjectMenu, "project menu opens");
    ui.dispatch({Action::Back});
    expect(ui.state().modal == Modal::None, "back closes project menu");

    ui.setModified(true);
    ui.dispatch({Action::ProjectMenu});
    ui.dispatch({Action::Confirm});
    expect(ui.state().modal == Modal::ConfirmDiscard,
           "new project requests confirmation when changes are unsaved");
    expect(ui.state().discardChoice == DiscardChoice::Cancel,
           "discard confirmation defaults to the safe cancel choice");
    ui.dispatch({Action::Confirm});
    expect(ui.state().modal == Modal::ProjectMenu &&
               ui.takeProjectRequest() == ProjectRequest::None,
           "default cancel does not discard or request a new project");
    ui.dispatch({Action::Confirm});
    ui.dispatch({Action::NavUp});
    ui.dispatch({Action::Confirm});
    expect(ui.state().modal == Modal::None &&
               ui.takeProjectRequest() == ProjectRequest::NewProject,
           "discard choice emits an explicit new-project request");
    expect(ui.state().modified,
           "UI waits for project layer to confirm success before clearing dirty state");

    ui.dispatch({Action::ScreenNext});
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavDown});
    expect(ui.state().editField == EditField::Offset,
           "edit field navigation reaches offset");
    ui.dispatch({Action::ValueFine, true});
    ui.dispatch({Action::NavRight});
    expect(sequencer.track(0).rhythm.offset == 1,
           "fine modifier is retained for following value change");
    ui.dispatch({Action::Back});
    expect(!ui.state().fineAdjust, "back releases fine-adjust mode first");
}

void testSystemThemeSelection() {
    using namespace pocket;
    using namespace pocket::ui;

    Sequencer sequencer;
    const platform::SystemCapabilities capabilities{
        true, true, true, true, true, true};
    UiController ui(sequencer, capabilities);
    for (int screen = 0; screen < 6; ++screen) {
        ui.dispatch({Action::ScreenNext});
    }
    expect(ui.state().screen == Screen::System,
           "system screen is reachable for display theme selection");

    for (int category = 0; category < 4; ++category) {
        ui.dispatch({Action::NavDown});
    }
    expect(ui.state().systemCategory == platform::SettingsCategory::Display,
           "system navigation reaches display settings");

    expect(ui.systemSettings().display.theme == platform::ThemeId::QuietDark,
           "system theme defaults to Quiet Dark");
    ui.dispatch({Action::NavRight});
    expect(ui.systemSettings().display.theme == platform::ThemeId::WarmPaper &&
               !ui.state().modified,
           "display settings switch theme without marking the musical project dirty");
    const platform::ThemeId themeOrder[] = {
        platform::ThemeId::HighContrast, platform::ThemeId::NeonRgb,
        platform::ThemeId::Outrun,       platform::ThemeId::AmberCrt,
        platform::ThemeId::Phosphor,     platform::ThemeId::VgaDos,
        platform::ThemeId::QuietDark};
    for (const auto expected : themeOrder) {
        ui.dispatch({Action::NavRight});
        expect(ui.systemSettings().display.theme == expected &&
                   !ui.state().modified,
               "theme selection advances through the catalog and wraps forward");
    }
    ui.dispatch({Action::NavLeft});
    expect(ui.systemSettings().display.theme == platform::ThemeId::VgaDos,
           "theme selection wraps backward");

    ui.dispatch({Action::NavUp});
    expect(ui.state().systemCategory == platform::SettingsCategory::InputAndControls,
           "vertical navigation still changes system categories");
    const auto theme = ui.systemSettings().display.theme;
    ui.dispatch({Action::NavRight});
    expect(ui.systemSettings().display.theme == theme,
           "horizontal navigation outside Display does not alter the theme");
}


void testEffectiveTrackStateAndTempoControls() {
    using namespace pocket;
    using namespace pocket::ui;

    Sequencer sequencer;
    sequencer.setInstrument(0, InstrumentType::Synth);
    audio::AudioParameterQueue queue;
    UiController ui(sequencer);
    ui.setAudioParameterQueue(&queue);

    // Live audio edits update effectiveTrackState and MainScreenSnapshot shadow while keeping sequencer.track untouched
    ui.dispatch({Action::MuteToggle});
    const auto effectiveAfterMute = ui.effectiveTrackState(0);
    expect(effectiveAfterMute.muted && !sequencer.track(0).muted,
           "effectiveTrackState reflects live audio shadow mute while sequencer track stays unmodified");

    MainScreenSnapshot liveSnapshot{};
    expect(makeMainScreenSnapshot(
               sequencer, ui.state().selectedTrack, ui.state().audioTrackSettings, liveSnapshot) &&
               liveSnapshot.tracks[0].muted,
           "MainScreenSnapshot overload overlays live audioTrackSettings mute state");

    // Edit screen volume & mute left/right
    ui.dispatch({Action::ScreenNext}); // Edit
    for (int i = 0; i < 4; ++i) {
        ui.dispatch({Action::NavDown});
    }
    expect(ui.state().editField == EditField::Volume, "reach edit volume field");
    ui.dispatch({Action::NavLeft});
    expect(ui.effectiveTrackState(0).volume == 95 && sequencer.track(0).volume == 100,
           "effectiveTrackState reflects queued live volume change");
    ui.dispatch({Action::NavDown});
    expect(ui.state().editField == EditField::Mute, "reach edit mute field");
    ui.dispatch({Action::NavRight});
    expect(!ui.effectiveTrackState(0).muted,
           "EDIT > Mute supports Left/Right toggle");

    // Sound screen filter Confirm toggle and effectiveTrackState
    ui.dispatch({Action::ScreenNext}); // Sound
    ui.dispatch({Action::NavDown});
    ui.dispatch({Action::NavDown});
    expect(ui.state().soundField == SoundField::FilterEnabled, "reach sound filter field");
    ui.dispatch({Action::Confirm});
    expect(ui.effectiveTrackState(0).filter.enabled && !sequencer.track(0).filter.enabled,
           "SOUND > FilterEnabled supports Confirm toggle and updates effectiveTrackState");

    // Tempo controls on MAIN and SYSTEM > ClockAndTransport
    Sequencer tempoSeq;
    const platform::SystemCapabilities caps{true, true, true, true, true, true};
    UiController tempoUi(tempoSeq, caps);
    expect(tempoUi.systemSettings().clock.initialTempoBpm == 120,
           "systemSettings clock tempo initializes from sequencer tempo");
    tempoUi.dispatch({Action::NavRight});
    expect(tempoSeq.tempo() == 125.0 &&
               tempoUi.systemSettings().clock.initialTempoBpm == 125,
           "MAIN screen Left/Right adjusts tempo by 5 BPM coarse");
    tempoUi.dispatch({Action::NavLeft, true});
    expect(tempoSeq.tempo() == 124.0 &&
               tempoUi.systemSettings().clock.initialTempoBpm == 124,
           "MAIN screen fine Left/Right adjusts tempo by 1 BPM");

    for (int s = 0; s < 6; ++s) {
        tempoUi.dispatch({Action::ScreenNext});
    }
    expect(tempoUi.state().screen == Screen::System &&
               tempoUi.state().systemCategory == platform::SettingsCategory::ClockAndTransport,
           "reach SYSTEM > ClockAndTransport");
    tempoUi.dispatch({Action::NavRight});
    expect(tempoSeq.tempo() == 129.0 &&
               tempoUi.systemSettings().clock.initialTempoBpm == 129,
           "SYSTEM > ClockAndTransport Left/Right adjusts tempo");
    // Synth and Delay parameters on SOUND screen
    Sequencer synthDelaySeq;
    audio::AudioParameterQueue synthDelayQueue;
    UiController synthDelayUi(synthDelaySeq);
    synthDelayUi.setAudioParameterQueue(&synthDelayQueue);
    synthDelayUi.dispatch({Action::ScreenNext}); // Edit
    synthDelayUi.dispatch({Action::ScreenNext}); // Sound
    for (int i = 0; i < 6; ++i) {
        synthDelayUi.dispatch({Action::NavDown});
    }
    expect(synthDelayUi.state().soundField == SoundField::Pitch,
           "SOUND navigation reaches Synth Pitch field");
    synthDelayUi.dispatch({Action::NavRight, false, true});
    expect(synthDelayUi.effectiveTrackState(0).synth.pitchHz == 120.0F &&
               synthDelaySeq.track(0).synth.pitchHz == 110.0F,
           "live Synth Pitch edit updates effectiveTrackState and queues command");
    synthDelayUi.dispatch({Action::NavDown});
    expect(synthDelayUi.state().soundField == SoundField::Morph,
           "SOUND navigation reaches Synth Morph field");
    synthDelayUi.dispatch({Action::NavRight, false, true});
    expect(std::abs(synthDelayUi.effectiveTrackState(0).synth.morph - 0.55F) < 1e-4F,
           "live Synth Morph edit updates effectiveTrackState");
    for (int i = 0; i < 4; ++i) {
        synthDelayUi.dispatch({Action::NavDown});
    }
    expect(synthDelayUi.state().soundField == SoundField::DelayTime,
           "SOUND navigation reaches DelayTime field");
    synthDelayUi.dispatch({Action::NavRight, false, true});
    expect(synthDelayUi.effects().delayMilliseconds == 275,
           "SOUND > DelayTime adjusts delay time in ms");
    synthDelayUi.dispatch({Action::NavDown});
    expect(synthDelayUi.state().soundField == SoundField::DelayFeedback,
           "SOUND navigation reaches DelayFeedback field");
    synthDelayUi.dispatch({Action::NavRight, false, true});
    expect(std::abs(synthDelayUi.effects().delayFeedback - 0.30F) < 1e-4F,
           "SOUND > DelayFeedback adjusts feedback ratio");
    synthDelayUi.dispatch({Action::NavDown});
    expect(synthDelayUi.state().soundField == SoundField::DelayMix,
           "SOUND navigation reaches DelayMix field");
    synthDelayUi.dispatch({Action::NavRight, false, true});
    expect(std::abs(synthDelayUi.effects().delayMix - 0.25F) < 1e-4F,
           "SOUND > DelayMix adjusts wet mix ratio");
}


} // namespace

int main() {
    testKeyboardAndControllerMappings();
    testAudioDeviceSpec();
    testInputRepeater();
    testStepLightEnvelope();
    testStepTriggerEventQueue();
    testStepLightBank();
    testThemeCatalogCycling();
    testMainScreenSnapshot();
    testLiveModulationOverlay();
    testJitterOverlayFallback();
    testMainScreenLayout();
    testMainScreenDrawList();
    testTrackNavigationAndTransport();
    testSynthEngineSelection();
    testSampleBrowser();
    testUiAudioParameterQueue();
    testScreensAndEditRanges();
    testParameterColumnNavigation();
    testModalsAndFineMode();
    testSystemThemeSelection();
    testEffectiveTrackStateAndTempoControls();

    if (failures != 0) {
        std::cerr << failures << " UI test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All UI tests passed\n";
    return EXIT_SUCCESS;
}
