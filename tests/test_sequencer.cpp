#include "sequencer/EuclideanGenerator.h"
#include "audio/AudioCallbackProcessor.h"
#include "audio/BasicVaVoice.h"
#include "audio/Mixer.h"
#include "audio/OnePoleLowPass.h"
#include "audio/PlaitsModels.h"
#include "audio/ReverbEffect.h"
#include "audio/SampleCache.h"
#include "audio/SamplerMixerRenderer.h"
#include "audio/SamplerVoicePool.h"
#include "audio/SelectableSynthVoice.h"
#include "audio/WavDecoder.h"
#include "sequencer/Modulation.h"
#include "sequencer/MidiClock.h"
#include "sequencer/MidiNote.h"
#include "sequencer/Sequencer.h"
#include "platform/SystemSettings.h"
#include "platform/BackendInterfaces.h"
#include "project/ProjectData.h"
#include "project/ProjectStore.h"
#include "platform/SettingsStore.h"
#include "audio/DelayEffect.h"
#include "ui/ThemePalette.h"
#include "ui/UiController.h"

#include <cstdlib>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void append16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void appendId(std::vector<std::uint8_t>& bytes, const char* id) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>(id[i]));
    }
}

void appendChunk(std::vector<std::uint8_t>& bytes,
                 const char* id,
                 const std::vector<std::uint8_t>& data) {
    appendId(bytes, id);
    append32(bytes, static_cast<std::uint32_t>(data.size()));
    bytes.insert(bytes.end(), data.begin(), data.end());
    if ((data.size() & 1U) != 0) {
        bytes.push_back(0);
    }
}

std::vector<std::uint8_t> makeWave(
    std::uint16_t format,
    std::uint16_t channels,
    std::uint32_t sampleRate,
    std::uint16_t bitsPerSample,
    const std::vector<std::uint8_t>& sampleData,
    bool extensible = false,
    bool addOddChunk = false) {
    const auto blockAlign = static_cast<std::uint16_t>(
        channels * static_cast<std::uint16_t>(bitsPerSample / 8));
    std::vector<std::uint8_t> fmt;
    append16(fmt, extensible ? 0xfffeU : format);
    append16(fmt, channels);
    append32(fmt, sampleRate);
    append32(fmt, sampleRate * blockAlign);
    append16(fmt, blockAlign);
    append16(fmt, bitsPerSample);
    if (extensible) {
        append16(fmt, 22);
        append16(fmt, bitsPerSample);
        append32(fmt, 0);
        append32(fmt, format);
        append16(fmt, 0);
        append16(fmt, 0x0010);
        const std::array<std::uint8_t, 8> guidTail{
            0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
        fmt.insert(fmt.end(), guidTail.begin(), guidTail.end());
    }

    std::vector<std::uint8_t> body;
    appendId(body, "WAVE");
    appendChunk(body, "fmt ", fmt);
    if (addOddChunk) {
        appendChunk(body, "JUNK", {0x42});
    }
    appendChunk(body, "data", sampleData);

    std::vector<std::uint8_t> file;
    appendId(file, "RIFF");
    append32(file, static_cast<std::uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class TestSynthVoice final : public pocket::audio::ISynthVoice {
public:
    bool setParameters(
        const pocket::audio::SynthParameters& newParameters) noexcept override {
        parameters = newParameters;
        return acceptParameters;
    }

    bool noteOn(float newVelocity) noexcept override {
        velocity = newVelocity;
        active = true;
        ++noteOnCount;
        return true;
    }

    void noteOff() noexcept override { active = false; }

    bool render(float* output,
                std::size_t frameCount,
                std::uint32_t) noexcept override {
        if (output == nullptr) {
            return false;
        }
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            output[frame] = active ? velocity * parameters.level : 0.0F;
        }
        return true;
    }

    bool isRealtimeSafe() const noexcept override { return realtimeSafe; }

    pocket::audio::SynthParameters parameters{};
    float velocity = 0.0F;
    unsigned int noteOnCount = 0;
    bool active = false;
    bool acceptParameters = true;
    bool realtimeSafe = true;
};

void testPulseCountAndRotation() {
    for (std::uint8_t length = pocket::EuclideanGenerator::MinLength;
         length <= pocket::EuclideanGenerator::MaxLength;
         ++length) {
        for (std::uint8_t fill = 1; fill <= length; ++fill) {
            for (std::uint8_t offset : {std::uint8_t{0}, std::uint8_t{1},
                                        std::uint8_t{31}}) {
                const pocket::EuclideanConfig config{length, fill, offset};
                const auto pattern = pocket::EuclideanGenerator::generate(config);
                unsigned int pulseCount = 0;
                for (const bool active : pattern) {
                    pulseCount += active ? 1U : 0U;
                }
                expect(pattern.size() == length, "pattern has configured length");
                expect(pulseCount == fill, "pattern preserves requested pulse count");
            }
        }
    }
}

void testEvenDistribution() {
    for (std::uint8_t length = 2; length <= 32; ++length) {
        for (std::uint8_t fill = 1; fill <= length; ++fill) {
            const auto pattern = pocket::EuclideanGenerator::generate({length, fill, 0});
            for (std::uint8_t start = 0; start < length; ++start) {
                for (std::uint8_t width = 1; width <= length; ++width) {
                    unsigned int count = 0;
                    for (std::uint8_t offset = 0; offset < width; ++offset) {
                        count += pattern[(start + offset) % length] ? 1U : 0U;
                    }
                    expect(count == (static_cast<unsigned int>(width) * fill) / length ||
                               count == (static_cast<unsigned int>(width) * fill +
                                         length - 1) / length,
                           "cyclic windows differ by at most one pulse");
                }
            }
        }
    }
}

void testOffsetModuloAndBoundaries() {
    expect(pocket::EuclideanGenerator::generate({8, 3, 0}) ==
               std::vector<bool>({true, false, false, true, false, false, true, false}),
           "step zero is active and unrotated pulses are evenly spaced");
    expect(pocket::EuclideanGenerator::generate({8, 3, 1}) ==
               std::vector<bool>({false, true, false, false, true, false, false, true}),
           "positive offset rotates active steps toward higher indices");

    const pocket::EuclideanConfig first{7, 3, 1};
    const pocket::EuclideanConfig equivalent{7, 3, 8};
    expect(pocket::EuclideanGenerator::generate(first) ==
               pocket::EuclideanGenerator::generate(equivalent),
           "offsets equivalent modulo pattern length produce equal patterns");

    expect(pocket::EuclideanGenerator::generate({2, 1, 31}).size() == 2,
           "minimum length and maximum offset are valid");
    expect(pocket::EuclideanGenerator::generate({32, 32, 0}).size() == 32,
           "maximum length and full fill are valid");
    expect(!pocket::EuclideanGenerator::isValid({16, 0, 0}),
           "zero fill is outside the specified parameter range");
    expect(!pocket::EuclideanGenerator::isValid({16, 17, 0}),
           "fill cannot exceed length");
}

void testIndependentTracksAndTransport() {
    pocket::Sequencer sequencer;
    expect(sequencer.setRhythm(0, {2, 1, 0}), "configure track zero");
    expect(sequencer.setRhythm(1, {3, 1, 0}), "configure track one");

    const auto stoppedTick = sequencer.tick();
    expect(!stoppedTick.triggered[0] && sequencer.track(0).position == 0,
           "stopped transport does not advance tracks");

    sequencer.start();
    const auto first = sequencer.tick();
    expect(first.triggered[0] && first.triggered[1] &&
               first.stepPositions[0] == 0 && first.stepPositions[1] == 0,
           "first tick reports both triggers and their actual step positions");
    sequencer.tick();
    expect(sequencer.track(0).position == 0 && sequencer.track(1).position == 2,
           "track cycles wrap independently");
    sequencer.tick();
    expect(sequencer.track(0).position == 1 && sequencer.track(1).position == 0,
           "shorter cycle does not reset another track");

    sequencer.stop();
    expect(sequencer.track(0).position == 1 && sequencer.track(1).position == 0,
           "stop halts the clock without altering the stopped positions");
    expect(sequencer.masterTickPosition() == 3,
           "master clock counts shared sixteenth-note ticks");
    sequencer.tick();
    expect(sequencer.masterTickPosition() == 3,
           "stopped clock does not advance its master position");

    sequencer.start();
    expect(sequencer.track(0).position == 0 && sequencer.track(1).position == 0 &&
               sequencer.masterTickPosition() == 0,
           "START resets every track and the master clock phase together");
    const auto restarted = sequencer.tick();
    expect(restarted.triggered[0] && restarted.triggered[1] &&
               restarted.stepPositions[0] == 0 &&
               restarted.stepPositions[1] == 0 &&
               restarted.masterTickPosition == 0,
           "restarted transport reports all triggers at step zero");
    sequencer.start();
    expect(sequencer.masterTickPosition() == 1,
           "calling START while already playing does not reset the running clock");
}

void testExamplePresetCyclesIndependently() {
    pocket::Sequencer sequencer;
    for (std::size_t i = 0; i < pocket::TrackCount; ++i) {
        expect(pocket::EuclideanGenerator::isValid(pocket::ExamplePresetRhythms[i]),
               "example preset has valid rhythm parameters");
        expect(sequencer.setRhythm(i, pocket::ExamplePresetRhythms[i]),
               "example preset rhythm can be assigned to each track");
    }

    sequencer.start();
    const auto initial = sequencer.tick();
    for (int i = 1; i < 7; ++i) {
        sequencer.tick();
    }
    const auto afterSevenTicks = sequencer.tick();
    expect(initial.triggered != afterSevenTicks.triggered,
           "different pattern lengths produce a changing combined rhythm");

    constexpr std::uint64_t FullPresetCycleTicks = 18480;
    for (std::uint64_t i = 8; i < FullPresetCycleTicks; ++i) {
        sequencer.tick();
    }
    expect(sequencer.masterTickPosition() == FullPresetCycleTicks,
           "master clock tracks the complete multi-pattern cycle");
    for (std::size_t i = 0; i < pocket::TrackCount; ++i) {
        expect(sequencer.track(i).position == 0,
               "all example track cycles realign at their least common multiple");
    }
}

void testLfoModulationAndDeterminism() {
    pocket::ModulationEngine engine;
    pocket::LfoConfig config;
    config.enabled = true;
    config.waveform = pocket::LfoWaveform::Sine;
    config.rateMode = pocket::LfoRateMode::Free;
    config.rateHz = 0.25;
    config.destination = pocket::ModulationDestination::Length;
    config.amount = 30;
    const pocket::EffectiveRhythm base{16, 4, 0};

    const auto first = engine.advance(0, config, base, 0.125, 120.0);
    expect(first && first->length == 22 && first->fill == 4 && first->offset == 0,
           "LFO output is deterministically quantized to integer rhythm values");
    config.destination = pocket::ModulationDestination::Fill;
    config.amount = 31;
    const auto clamped = engine.advance(0, config, {2, 2, 31}, 0.125, 120.0);
    expect(clamped && clamped->length >= 2 && clamped->length <= 32 &&
               clamped->fill >= 1 && clamped->fill <= clamped->length &&
               clamped->offset <= 31,
           "effective rhythm values stay inside valid bounds");
    const auto shown = engine.display(0);
    expect(std::abs(shown.phase - 0.0625) < 1.0e-9 &&
               std::abs(shown.value - std::sin(2.0 * 3.14159265358979323846 *
                                                0.0625)) < 1.0e-9,
           "display exposes the LFO phase and waveform the audio thread used");
    expect(engine.display(pocket::TrackCount).phase == 0.0,
           "display rejects out-of-range tracks without touching state");

    pocket::Sequencer sequencer;
    pocket::LfoConfig sequencerLfo = config;
    sequencerLfo.destination = pocket::ModulationDestination::Length;
    sequencer.setLfo(0, sequencerLfo);
    sequencer.start();
    const auto beforeTick = sequencer.lfoDisplay(0);
    (void)sequencer.tick();
    const auto afterTick = sequencer.lfoDisplay(0);
    expect(afterTick.phase > beforeTick.phase &&
               std::abs(afterTick.phase - 0.03125) < 1.0e-9,
           "sequencer exposes the live LFO phase for the UI after a tick");

    pocket::LfoConfig invalid = config;
    invalid.rateHz = 100.0;
    expect(!pocket::ModulationEngine::isValid(invalid),
           "reject LFO rates outside the supported range");
    expect(!engine.advance(0, invalid, base, 0.125, 120.0),
           "report invalid LFO configurations instead of returning a base-value fallback");
    expect(std::abs(pocket::LfoRateCalculator::cycleQuarterNotes(
                        pocket::LfoDivision::DottedEighth) -
                    0.75) < 1.0e-12,
           "dotted synchronized values use their extended musical duration");
    expect(std::abs(pocket::LfoRateCalculator::cycleQuarterNotes(
                        pocket::LfoDivision::QuarterTriplet) -
                    2.0 / 3.0) < 1.0e-12,
           "triplet synchronized values use their musical duration");
    const std::array<double, 15> expectedDivisionBeats{{
        4.0, 6.0, 8.0 / 3.0,
        2.0, 3.0, 4.0 / 3.0,
        1.0, 1.5, 2.0 / 3.0,
        0.5, 0.75, 1.0 / 3.0,
        0.25, 0.375, 1.0 / 6.0,
    }};
    for (std::size_t i = 0; i < expectedDivisionBeats.size(); ++i) {
        const auto division = static_cast<pocket::LfoDivision>(i);
        expect(std::abs(pocket::LfoRateCalculator::cycleQuarterNotes(division) -
                        expectedDivisionBeats[i]) < 1.0e-12,
               "all straight, dotted, and triplet divisions have exact durations");
    }
    expect(pocket::ModulationEngine::MaxFreeRateHz == 1.0,
           "free-running rate stays below the master-tick sampling Nyquist limit");

    expect(pocket::LfoWaveformGenerator::value(
               pocket::LfoWaveform::SawUp, 0.0, 0.0) == -1.0 &&
               pocket::LfoWaveformGenerator::value(
                   pocket::LfoWaveform::SawDown, 0.0, 0.0) == 1.0,
           "saw waveforms have deterministic opposite ramps");

    pocket::LfoConfig synced = config;
    synced.rateMode = pocket::LfoRateMode::Synchronized;
    synced.division = pocket::LfoDivision::Quarter;
    const auto quarterCycle =
        pocket::LfoRateCalculator::phaseIncrement(synced, 0.5, 120.0);
    expect(quarterCycle && std::abs(*quarterCycle - 1.0) < 1.0e-12,
           "quarter-note synchronized LFO completes one cycle per beat");
    const auto slowTempoIncrement =
        pocket::LfoRateCalculator::phaseIncrement(synced, 0.125, 120.0);
    const auto fastTempoIncrement =
        pocket::LfoRateCalculator::phaseIncrement(synced, 0.125, 240.0);
    expect(slowTempoIncrement && fastTempoIncrement &&
               std::abs(*fastTempoIncrement - 2.0 * *slowTempoIncrement) <
                   1.0e-12,
           "synchronized rate follows master tempo changes");
    synced.rateMode = pocket::LfoRateMode::Free;
    const auto freeSlowTempo =
        pocket::LfoRateCalculator::phaseIncrement(synced, 0.125, 120.0);
    const auto freeFastTempo =
        pocket::LfoRateCalculator::phaseIncrement(synced, 0.125, 240.0);
    expect(freeSlowTempo && freeFastTempo &&
               *freeSlowTempo == *freeFastTempo,
           "free-running rate is independent of master tempo");

    std::array<pocket::LfoConfig, pocket::MaxLfosPerTrack> multipleLfos{};
    multipleLfos[0].enabled = true;
    multipleLfos[0].waveform = pocket::LfoWaveform::Square;
    multipleLfos[0].destination = pocket::ModulationDestination::Length;
    multipleLfos[0].amount = 2;
    multipleLfos[1] = multipleLfos[0];
    multipleLfos[1].destination = pocket::ModulationDestination::Fill;
    multipleLfos[1].amount = 1;
    multipleLfos[2] = multipleLfos[0];
    multipleLfos[2].amount = 1;
    pocket::ModulationEngine multiEngine;
    const auto combined =
        multiEngine.advance(0, multipleLfos, base, 0.125, 120.0);
    expect(combined && combined->length == 19 && combined->fill == 5,
           "multiple LFO slots sum contributions per assigned parameter");
    std::array<pocket::LfoConfig, pocket::MaxLfosPerTrack> atomicLfos{};
    atomicLfos[0].enabled = true;
    atomicLfos[0].waveform = pocket::LfoWaveform::Sine;
    atomicLfos[0].amount = 31;
    auto invalidLfos = atomicLfos;
    invalidLfos[3].rateHz = 99.0;
    pocket::ModulationEngine atomicEngine;
    expect(!atomicEngine.advance(0, invalidLfos, base, 0.125, 120.0),
           "reject an invalid configuration in any LFO slot");
    pocket::ModulationEngine freshAtomicEngine;
    const auto afterInvalid =
        atomicEngine.advance(0, atomicLfos, base, 0.125, 120.0);
    const auto fresh =
        freshAtomicEngine.advance(0, atomicLfos, base, 0.125, 120.0);
    expect(afterInvalid && fresh && afterInvalid->length == fresh->length &&
               afterInvalid->fill == fresh->fill,
           "invalid LFO configuration does not partially advance runtime phases");

    pocket::LfoConfig randomConfig;
    randomConfig.enabled = true;
    randomConfig.waveform = pocket::LfoWaveform::SampleAndHold;
    randomConfig.rateHz = 1.0;
    randomConfig.destination = pocket::ModulationDestination::Length;
    randomConfig.amount = 20;
    pocket::ModulationEngine randomA;
    pocket::ModulationEngine randomB;
    for (int i = 0; i < 12; ++i) {
        const auto a = randomA.advance(0, randomConfig, base, 0.25, 120.0);
        const auto b = randomB.advance(0, randomConfig, base, 0.25, 120.0);
        expect(a && b && a->length == b->length,
               "sample-and-hold waveform is reproducible after reset");
    }
    randomA.reset();
    std::optional<pocket::EffectiveRhythm> heldBeforeCycle;
    std::optional<pocket::EffectiveRhythm> heldAfterCycle;
    for (int i = 0; i < 4; ++i) {
        const auto output =
            randomA.advance(0, randomConfig, base, 0.25, 120.0);
        if (i == 0) {
            heldBeforeCycle = output;
        }
        if (i == 3) {
            heldAfterCycle = output;
        }
    }
    expect(heldBeforeCycle && heldAfterCycle &&
               heldBeforeCycle->length == base.length &&
               heldAfterCycle->length != heldBeforeCycle->length,
           "sample-and-hold updates once when a complete LFO cycle ends");
}

void testSequencerModulationPreservesMasterPhase() {
    pocket::Sequencer sequencer;
    expect(sequencer.setRhythm(0, {16, 4, 0}), "configure modulated track");
    pocket::LfoConfig lfo;
    lfo.enabled = true;
    lfo.rateHz = 0.25;
    lfo.destination = pocket::ModulationDestination::Length;
    lfo.amount = 30;
    expect(sequencer.setLfo(0, lfo), "assign per-track LFO");
    expect(!sequencer.setLfo(8, lfo), "reject LFO assignment to invalid track");
    lfo.rateHz = 99.0;
    expect(!sequencer.setLfo(0, lfo), "reject invalid LFO configuration");

    sequencer.start();
    sequencer.tick();
    const auto masterPhase = sequencer.masterTickPosition();
    const auto effective = sequencer.track(0).effectiveRhythm;
    expect(effective.length != sequencer.track(0).rhythm.length,
           "LFO independently changes effective length from stored base length");
    expect(sequencer.track(0).rhythm.length == 16 &&
               sequencer.track(0).rhythm.fill == 4,
           "modulation does not overwrite stored base rhythm values");
    expect(sequencer.track(0).position < effective.length,
           "phase is normalized to the modulated pattern length");
    sequencer.tick();
    expect(sequencer.masterTickPosition() == masterPhase + 1,
           "pattern-length modulation does not reset master transport");
}

void testJitterModesAndDeterminism() {
    using namespace pocket;

    const EuclideanConfig source{8, 4, 0};
    JitterConfig config;
    JitterEngine engine;
    std::array<JitterConfig, JitterTrackCount> configs{};
    engine.reset(configs);
    for (std::uint8_t step = 0; step < source.length; ++step) {
        const auto result = engine.process(0, config, source, step, step);
        expect(result && *result == EuclideanGenerator::isActive(source, step),
               "disabled jitter exactly preserves the source Euclidean pattern");
    }
    bool maskValid = false;
    expect(!engine.eventMask(0, maskValid) && !maskValid,
           "jitter display mask is invalid before enabled decisions are built");

    config.enabled = true;
    config.mode = JitterMode::Insert;
    config.intensity = 100;
    expect(engine.process(0, config, source, 0, 0).value_or(false),
           "insert mode preserves original active events");
    unsigned int insertedTriggers = 0;
    for (std::uint8_t step = 0; step < source.length; ++step) {
        insertedTriggers += engine.process(0, config, source, step, step)
                                .value_or(false)
                                ? 1U
                                : 0U;
    }
    expect(insertedTriggers == source.length,
           "100 percent insert fills inactive positions without changing source");
    expect(engine.eventMask(0, maskValid) && maskValid &&
               engine.eventMask(0, maskValid) == 0xFFU,
           "insert mode exposes every trigger position in the display mask");
    config.intensity = 0;
    engine.invalidate(0);
    unsigned int zeroIntensityTriggers = 0;
    for (std::uint8_t step = 0; step < source.length; ++step) {
        zeroIntensityTriggers += engine.process(0, config, source, step, step)
                                     .value_or(false)
                                     ? 1U
                                     : 0U;
    }
    expect(zeroIntensityTriggers == source.fill,
           "zero jitter intensity reproduces the original source pattern");

    config.mode = JitterMode::Drop;
    config.intensity = 100;
    config.rate = JitterRate::Sixteenth;
    engine.reset(configs);
    unsigned int droppedTriggers = 0;
    for (std::uint8_t step = 0; step < source.length; ++step) {
        droppedTriggers += engine.process(0, config, source, step, step)
                               .value_or(true)
                               ? 1U
                               : 0U;
    }
    expect(droppedTriggers == 0,
           "100 percent drop suppresses all active events without altering rhythm");

    config.mode = JitterMode::Shift;
    config.shiftRange = 2;
    config.useCustomSeed = true;
    config.randomSeed = 123456;
    JitterEngine firstEngine;
    JitterEngine secondEngine;
    firstEngine.reset(configs);
    secondEngine.reset(configs);
    std::array<bool, 8> firstPlan{};
    std::array<bool, 8> secondPlan{};
    for (std::uint8_t step = 0; step < source.length; ++step) {
        firstPlan[step] =
            firstEngine.process(0, config, source, step, step).value_or(false);
        secondPlan[step] =
            secondEngine.process(0, config, source, step, step).value_or(false);
    }
    expect(firstPlan == secondPlan,
           "custom seed reproduces shifted trigger placement");
    unsigned int shiftedTriggers = 0;
    for (const bool event : firstPlan) {
        shiftedTriggers += event ? 1U : 0U;
    }
    expect(shiftedTriggers <= source.fill,
           "shift collisions never create duplicate trigger events");
    const EuclideanConfig shortPattern{2, 1, 0};
    config.shiftRange = MaxJitterShiftTicks;
    JitterEngine boundaryEngine;
    boundaryEngine.reset(configs);
    unsigned int boundaryTriggers = 0;
    for (std::uint8_t step = 0; step < shortPattern.length; ++step) {
        const auto event = boundaryEngine.process(
            0, config, shortPattern, step, step);
        expect(event.has_value(),
               "large signed shifts wrap safely across a short pattern boundary");
        boundaryTriggers += event.value_or(false) ? 1U : 0U;
    }
    expect(boundaryTriggers <= shortPattern.fill,
           "wrapping shift collisions suppress duplicate trigger events");
    config.mode = JitterMode::Mix;
    config.intensity = 100;
    JitterEngine mixEngine;
    mixEngine.reset(configs);
    unsigned int mixedTriggers = 0;
    for (std::uint8_t step = 0; step < source.length; ++step) {
        mixedTriggers += mixEngine.process(0, config, source, step, step)
                             .value_or(false)
                             ? 1U
                             : 0U;
    }
    expect(mixedTriggers ==
               static_cast<unsigned int>(source.length - source.fill),
           "mix applies drop, shift, and insert in the documented order");

    config.mode = JitterMode::Insert;
    config.intensity = 50;
    config.rate = JitterRate::Whole;
    config.useCustomSeed = true;
    config.randomSeed = 9001;
    JitterEngine rateEngine;
    rateEngine.reset(configs);
    const EuclideanConfig ratePattern{8, 4, 0};
    const auto firstRateDecision =
        rateEngine.process(0, config, ratePattern, 1, 0);
    bool decisionsStable = firstRateDecision.has_value();
    for (std::uint64_t tick = 1; tick < 16; ++tick) {
        const auto decision =
            rateEngine.process(0, config, ratePattern, 1, tick);
        decisionsStable = decisionsStable && decision == firstRateDecision;
    }
    expect(decisionsStable,
           "jitter decisions remain stable until the synchronized rate boundary");

    config.mode = JitterMode::Mix;
    config.intensity = 101;
    expect(!JitterEngine::isValid(config),
           "reject jitter intensity outside 0 to 100 percent");
    config.intensity = 100;
    config.shiftRange = MaxJitterShiftTicks + 1;
    expect(!JitterEngine::isValid(config),
           "reject shift range beyond its supported tick bound");
    expect(JitterEngine::ticksPerInterval(JitterRate::Quarter) == 4 &&
               JitterEngine::ticksPerInterval(JitterRate::Whole) == 16,
           "jitter rates are fixed musical intervals of the master clock");
}

void testSequencerJitterTransportAndProjectState() {
    using namespace pocket;

    Sequencer sequencer;
    expect(sequencer.setRhythm(0, {8, 4, 0}),
           "configure jitter transport test");
    JitterConfig jitter;
    jitter.enabled = true;
    jitter.mode = JitterMode::Drop;
    jitter.intensity = 100;
    jitter.rate = JitterRate::Quarter;
    expect(sequencer.setJitter(0, jitter), "assign per-track jitter");
    expect(!sequencer.setJitter(TrackCount, jitter),
           "reject jitter assignment outside the track range");

    sequencer.start();
    const auto started = sequencer.tick();
    expect(!started.triggered[0] && started.masterTickPosition == 0,
           "jitter starts at master transport phase zero");
    const auto phaseAfterStart = sequencer.masterTickPosition();
    expect(sequencer.setTempo(90.0), "change tempo while jitter is enabled");
    sequencer.stop();
    expect(sequencer.masterTickPosition() == phaseAfterStart,
           "stop freezes jitter and master clock state");
    sequencer.start();
    const auto restarted = sequencer.tick();
    expect(!restarted.triggered[0] && restarted.masterTickPosition == 0,
           "start resets deterministic jitter with the master transport");
    expect(sequencer.track(0).rhythm.length == 8 &&
               sequencer.track(0).rhythm.fill == 4,
           "jitter never destructively changes the stored Euclidean pattern");

    bool jitterMaskValid = false;
    expect(sequencer.jitterEventsMask(0, jitterMaskValid) == 0 &&
               jitterMaskValid,
           "sequencer exposes a valid all-dropped jitter mask");
    expect(!sequencer.jitterEventsMask(1, jitterMaskValid) && !jitterMaskValid,
           "tracks without jitter report no live display mask");

    const project::ProjectData project;
    expect(!project.tracks[0].jitter.enabled,
           "project stores independent per-track jitter settings");
}

void testTrackMixerSettingsAndMeters() {
    using namespace pocket;

    Sequencer sequencer;
    expect(sequencer.setPan(0, -1.0F), "set track hard-left pan");
    expect(sequencer.track(0).pan == -1.0F,
           "track stores normalized pan at the left boundary");
    expect(sequencer.setPan(0, 0.0F), "set track center pan");
    expect(!sequencer.setPan(0, 1.01F),
           "reject track pan outside normalized range");
    expect(!sequencer.setPan(TrackCount, 0.0F),
           "reject pan assignment outside the track range");
    sequencer.setVolume(0, 64);
    sequencer.setMuted(0, true);
    expect(sequencer.setReverbSend(0, 0.35F) &&
               sequencer.setDelaySend(0, 0.60F),
           "set track effect send levels");
    expect(!sequencer.setReverbSend(0, -0.01F) &&
               !sequencer.setReverbSend(0, 1.01F) &&
               !sequencer.setDelaySend(TrackCount, 0.5F),
           "reject effect send levels outside the normalized range");
    const auto mixerSettings = audio::Mixer::settingsForTrack(sequencer.track(0));
    expect(mixerSettings.volumePercent == 64 && mixerSettings.pan == 0.0F &&
               mixerSettings.muted &&
               mixerSettings.reverbSend == 0.35F &&
               mixerSettings.delaySend == 0.60F,
           "mixer adapter forwards per-track project settings to audio processing");

    const project::ProjectData project;
    expect(project.tracks[0].pan == 0.0F &&
               project.tracks[0].volume == 100,
           "project stores track mixer settings independently per track");
    expect(project.tracks[0].envelope.attackSeconds == 0.005F &&
               project.tracks[0].envelope.decaySeconds == 0.45F,
           "project stores per-track AD envelope defaults");
    expect(!project.tracks[0].filter.enabled &&
               project.tracks[0].filter.cutoffHz == 12000.0F,
           "project stores per-track filter defaults");
    expect(project.formatVersion == project::ProjectFormatVersion,
           "mixer and envelope settings are covered by the project format version");
}

void testAudioMixerProcessingAndMeterBridge() {
    using namespace pocket::audio;

    Mixer mixer;
    std::array<std::array<float, 1024>, pocket::TrackCount> leftInputs{};
    std::array<std::array<float, 1024>, pocket::TrackCount> rightInputs{};
    std::array<StereoInputBuffer, pocket::TrackCount> inputs{};
    std::array<TrackMixerSettings, pocket::TrackCount> settings{};
    for (std::size_t track = 0; track < pocket::TrackCount; ++track) {
        inputs[track] = {
            leftInputs[track].data(),
            track == 0 ? nullptr : rightInputs[track].data()};
    }
    leftInputs[0].fill(1.0F);
    std::array<float, 1024> outputLeft{};
    std::array<float, 1024> outputRight{};
    const StereoOutputBuffer output{outputLeft.data(), outputRight.data()};

    expect(Mixer::volumePercentToGain(0) == 0.0F &&
               Mixer::volumePercentToGain(100) == 1.0F,
           "track volume maps the UI percentage into a bounded internal gain");
    expect(Mixer::panLeftGain(-1.0F) == 1.0F &&
               Mixer::panRightGain(-1.0F) == 0.0F &&
               std::abs(Mixer::panLeftGain(0.0F) -
                        Mixer::panRightGain(0.0F)) < 1.0e-6F,
           "normalized pan maps left, center, and right consistently");
    expect(mixer.processBlock(inputs, settings, 100, 48000, output,
                              outputLeft.size()),
           "mixer processes interleaved-independent stereo channel buffers");
    expect(outputLeft.back() > 0.0F && outputRight.back() > 0.0F,
           "mono source is center-panned to both output channels");
    expect(mixer.meters().track(0) > 0.0F &&
               mixer.meters().master() > 0.0F,
           "audio-thread peak values are published to atomic UI-readable meters");

    settings[0] = {};
    settings[0].reverbSend = 1.0F;
    settings[0].delaySend = 0.5F;
    std::array<float, 2048> reverbSend{};
    std::array<float, 2048> delaySend{};
    const SendBuffers sends{reverbSend.data(), delaySend.data()};
    expect(mixer.processBlock(inputs, settings, 100, 48000, output,
                              outputLeft.size(), &sends),
           "mixer writes post-fader send buses");
    expect(reverbSend[2046] > 0.0F && delaySend[2046] > 0.0F &&
               std::abs(reverbSend[2046] - 2.0F * delaySend[2046]) < 1.0e-3F,
           "send bus levels follow their per-track amounts without changing dry audio");
    settings[0].reverbSend = 1.5F;
    expect(!mixer.processBlock(inputs, settings, 100, 48000, output,
                               outputLeft.size(), &sends),
           "mixer rejects out-of-range send levels");
    settings[0] = {};

    Mixer silentMixer;
    expect(silentMixer.processBlock(inputs, settings, 0, 48000, output, 16),
           "mixer accepts a zero global master volume");
    expect(outputLeft[15] == 0.0F && outputRight[15] == 0.0F,
           "master volume zero never produces an initial transient");

    settings[0].pan = -1.0F;
    expect(mixer.processBlock(inputs, settings, 100, 48000, output,
                              outputLeft.size()),
           "mixer accepts left-panned mono track");
    expect(outputLeft.back() > outputRight.back(),
           "mono panning moves signal to the selected stereo side");

    pocket::Sequencer transport;
    transport.start();
    transport.tick();
    const auto trackPosition = transport.track(0).position;
    const auto masterPosition = transport.masterTickPosition();
    transport.setMuted(0, true);
    settings[0] = Mixer::settingsForTrack(transport.track(0));
    expect(mixer.processBlock(inputs, settings, 100, 48000, output,
                              outputLeft.size()),
           "mute is applied in the mixer without changing sequencer state");
    expect(outputLeft.back() < 0.01F && outputRight.back() < 0.01F,
           "mute smoothly attenuates the audio output");
    expect(transport.track(0).position == trackPosition &&
               transport.masterTickPosition() == masterPosition,
           "mute smoothly attenuates audio and does not alter track position");

    settings[0].muted = false;
    settings[0].pan = 0.0F;
    rightInputs[0].fill(0.5F);
    inputs[0].right = rightInputs[0].data();
    expect(mixer.processBlock(inputs, settings, 100, 48000, output,
                              outputLeft.size()),
           "mixer processes stereo source buffers");
    expect(std::abs(outputLeft.back() - outputRight.back()) > 0.01F,
           "center-panned stereo source retains its stereo image");

    for (auto& channel : leftInputs) {
        channel.fill(1.0F);
    }
    for (auto& channel : rightInputs) {
        channel.fill(1.0F);
    }
    settings.fill({});
    expect(mixer.processBlock(inputs, settings, 100, 48000, output,
                              outputLeft.size()),
           "mixer handles simultaneous full-scale tracks");
    expect(mixer.meters().master() <= 1.0F &&
               outputLeft.back() <= 1.0F && outputRight.back() <= 1.0F,
           "internal summing headroom and soft limiter protect master output");

    const std::array<StereoInputBuffer, pocket::TrackCount> invalidInputs{};
    expect(!mixer.processBlock(invalidInputs, settings, 100, 48000, output, 16),
           "reject incomplete source buffers explicitly");
    settings[0].pan = 2.0F;
    expect(!mixer.processBlock(inputs, settings, 100, 48000, output, 16),
           "reject non-normalized pan before processing audio");
}

void testSamplerVoicePool() {
    pocket::audio::SamplerVoicePool voices;
    const std::array<float, 8> sample{{0.0F, 0.25F, 0.5F, 0.75F,
                                       1.0F, 0.75F, 0.5F, 0.25F}};
    expect(voices.assignSample(0, {sample.data(), sample.size(), 48000}),
           "sampler accepts finite preloaded mono PCM");
    expect(!voices.assignSample(pocket::TrackCount,
                                {sample.data(), sample.size(), 48000}) &&
               !voices.assignSample(1, {nullptr, sample.size(), 48000}) &&
               !voices.assignSample(1, {sample.data(), 0, 48000}) &&
               !voices.assignSample(1, {sample.data(), sample.size(), 7999}),
           "sampler rejects invalid tracks, buffers, lengths, and sample rates");
    expect(!voices.trigger(1) && !voices.trigger(0, 0.0F) &&
               !voices.trigger(0, std::numeric_limits<float>::infinity()) &&
               !voices.trigger(0, 4.1F),
           "sampler rejects unassigned tracks and invalid playback rates");

    expect(voices.trigger(0) && voices.activeVoiceCount() == 1 &&
               voices.activeVoiceCount(0) == 1,
           "sampler starts a voice within the fixed pool");
    std::array<float, 8> output;
    output.fill(-1.0F);
    expect(voices.renderTrack(0, output.data(), output.size(), 48000) &&
               output == sample && voices.activeVoiceCount() == 0,
           "sampler renders a finite voice and stops at its sample boundary");

    const std::array<float, 4> ramp{{0.0F, 1.0F, 0.0F, -1.0F}};
    expect(voices.assignSample(1, {ramp.data(), ramp.size(), 24000}) &&
               voices.trigger(1),
           "sampler accepts sample rate independent of output rate");
    std::array<float, 4> resampled{};
    expect(voices.renderTrack(1, resampled.data(), resampled.size(), 48000) &&
               resampled[0] == 0.0F && resampled[1] == 0.5F &&
               resampled[2] == 1.0F && resampled[3] == 0.5F,
           "sampler uses linear interpolation for rate conversion");

    voices.stopAll();
    expect(voices.activeVoiceCount() == 0 &&
               voices.trigger(1, 2.0F) &&
               voices.renderTrack(1, resampled.data(), 1, 48000) &&
               resampled[0] == 0.0F,
           "playback-rate multiplier changes deterministic sample progression");

    const std::array<float, 1> heldSample{{0.5F}};
    expect(voices.assignSample(0, {heldSample.data(), 1, 48000}),
           "sampler can replace sample while voices are stopped");
    voices.stopAll();
    for (std::size_t voice = 0;
         voice < pocket::audio::SamplerVoicePool::MaxVoices; ++voice) {
        expect(voices.trigger(0), "sampler fills its bounded voice pool");
    }
    expect(voices.activeVoiceCount() ==
               pocket::audio::SamplerVoicePool::MaxVoices &&
               voices.trigger(0) && voices.stolenVoiceCount() == 1 &&
               voices.activeVoiceCount(0) ==
                   pocket::audio::SamplerVoicePool::MaxVoices,
           "sampler steals the oldest voice deterministically at the polyphony limit");
    voices.stopTrack(0);
    expect(voices.activeVoiceCount() == 0 &&
               voices.activeVoiceCount(pocket::TrackCount) == 0,
           "sampler supports per-track stop and safe invalid-track queries");

    std::array<float, 2> unchanged{{7.0F, 8.0F}};
    expect(!voices.renderTrack(0, unchanged.data(), unchanged.size(), 0) &&
               unchanged[0] == 7.0F && unchanged[1] == 8.0F,
           "invalid render requests fail without partially changing output");

    const std::array<float, 3> queued{{0.1F, 0.2F, 0.3F}};
    expect(!voices.queueSample(pocket::TrackCount,
                               {queued.data(), queued.size(), 48000}, 7) &&
               !voices.queueSample(0, {nullptr, 1, 48000}, 7) &&
               !voices.queueSample(0, {queued.data(), 0, 48000}, 7) &&
               !voices.queueSample(0, {queued.data(), queued.size(), 48000}, 0),
           "queued sample swaps reject invalid requests");
    expect(voices.assignSample(0, {heldSample.data(), heldSample.size(), 48000}) &&
               voices.trigger(0) && voices.activeVoiceCount(0) == 1 &&
               voices.appliedSampleId(0) == 0,
           "prepare an active voice before queueing a sample swap");
    expect(voices.queueSample(0, {queued.data(), queued.size(), 48000}, 11),
           "sample swap is accepted on the UI-side queue");
    voices.applyPendingSamples();
    expect(voices.appliedSampleId(0) == 11 &&
               voices.activeVoiceCount(0) == 0 &&
               voices.sample(0).frameCount == queued.size(),
           "applying a swap stops old voices and publishes the sample id");
    std::array<float, 3> queuedOutput{};
    expect(voices.trigger(0) &&
               voices.renderTrack(0, queuedOutput.data(), queuedOutput.size(),
                                  48000) &&
               queuedOutput == queued,
           "the queued sample is audible after the swap");
}

void testSynthVoiceRack() {
    using namespace pocket::audio;
    SynthVoiceRack rack;
    std::array<TestSynthVoice, pocket::TrackCount> voices{};
    SynthParameters parameters;
    parameters.pitchHz = 220.0F;
    parameters.level = 0.75F;

    for (std::size_t track = 0; track < voices.size(); ++track) {
        expect(rack.configure(track, &voices[track], parameters),
               "synth rack configures one realtime-safe voice per track");
    }
    expect(rack.isRealtimeSafe() && rack.isConfigured(0) &&
               !rack.isConfigured(pocket::TrackCount),
           "synth rack validates fixed track coverage and realtime readiness");
    expect(!rack.configure(0, &voices[1], parameters) &&
               !rack.trigger(pocket::TrackCount) &&
               !rack.trigger(0, std::numeric_limits<float>::infinity()),
           "synth rack rejects shared voice instances and invalid triggers");

    std::array<float, 4> output{};
    expect(rack.trigger(0, 0.5F) &&
               rack.renderTrack(0, output.data(), output.size(), 48000) &&
               output[0] == 0.375F && voices[0].noteOnCount == 1,
           "synth rack dispatches note events and mono rendering");
    rack.stopTrack(0);
    expect(rack.renderTrack(0, output.data(), output.size(), 48000) &&
               output[0] == 0.0F,
           "synth rack forwards note-off to stop the voice");

    SynthVoiceRack incompleteRack;
    TestSynthVoice unsafeVoice;
    unsafeVoice.realtimeSafe = false;
    expect(!incompleteRack.configure(0, &unsafeVoice, parameters) &&
               !incompleteRack.isConfigured(0) &&
               incompleteRack.isRealtimeSafe(),
           "synth rack refuses unverified realtime implementations");
    TestSynthVoice rejectingVoice;
    rejectingVoice.acceptParameters = false;
    expect(!incompleteRack.configure(0, &rejectingVoice, parameters),
           "synth rack surfaces backend parameter setup failures");

    auto invalidParameters = parameters;
    invalidParameters.timbre = 1.1F;
    expect(!rack.configure(0, &voices[0], invalidParameters),
           "synth rack rejects out-of-range normalized parameters");

    pocket::Sequencer sequencer;
    pocket::AdEnvelopeSettings settings;
    settings.attackSeconds = 0.02F;
    settings.decaySeconds = 1.2F;
    expect(sequencer.setEnvelope(3, settings) &&
               sequencer.track(3).envelope.attackSeconds == 0.02F &&
               sequencer.track(3).envelope.decaySeconds == 1.2F,
           "sequencer stores independent per-track AD settings");
    expect(!sequencer.setEnvelope(pocket::TrackCount, settings),
           "sequencer rejects invalid AD track index");
    settings.attackSeconds = 0.0F;
    expect(!sequencer.setEnvelope(3, settings),
           "sequencer rejects attack outside supported range");
    settings.attackSeconds = 0.02F;
    settings.decaySeconds = std::numeric_limits<float>::infinity();
    expect(!sequencer.setEnvelope(3, settings),
           "sequencer rejects non-finite decay settings");
}

void testBasicVaVoice() {
    using namespace pocket::audio;
    BasicVaVoice voice;
    SynthParameters parameters;
    parameters.pitchHz = 440.0F;
    parameters.level = 0.8F;
    parameters.morph = 0.9F;
    parameters.color = 0.75F;
    expect(voice.setParameters(parameters) && voice.noteOn(0.75F),
           "basic VA voice accepts bounded oscillator settings");

    std::array<float, 6000> output{};
    expect(voice.render(output.data(), 256, 48000) && voice.isActive(),
           "basic VA voice renders a bounded active mono block");
    bool hasSignal = false;
    bool allFinite = true;
    for (std::size_t frame = 0; frame < 256; ++frame) {
        hasSignal = hasSignal || std::abs(output[frame]) > 1.0e-5F;
        allFinite = allFinite && std::isfinite(output[frame]) &&
                    std::abs(output[frame]) <= 1.0F;
    }
    expect(hasSignal && allFinite,
           "basic VA waveform is audible, finite, and level-bounded");
    auto envelopeSettings = pocket::AdEnvelopeSettings{};
    envelopeSettings.attackSeconds = 0.005F;
    envelopeSettings.decaySeconds = 0.1F;
    pocket::audio::AdEnvelope envelope;
    expect(envelope.trigger(envelopeSettings, 48000),
           "per-track AD envelope starts with valid settings");
    const float attackSample = envelope.process(1.0F);
    expect(attackSample > 0.0F && attackSample < 1.0F,
           "AD envelope applies its attack ramp to the oscillator signal");
    for (std::size_t frame = 1; frame < 5100; ++frame) {
        envelope.process(1.0F);
    }
    expect(!envelope.isActive() && envelope.process(1.0F) == 0.0F,
           "AD envelope completes a one-shot decay and gates silence");
    expect(!envelope.trigger(
               {std::numeric_limits<float>::quiet_NaN(), 0.1F}, 48000),
           "AD envelope rejects non-finite settings");

    auto aboveNyquist = parameters;
    aboveNyquist.pitchHz = 5000.0F;
    expect(voice.setParameters(aboveNyquist) &&
               !voice.render(output.data(), 1, 8000),
           "basic VA voice rejects pitch above the output Nyquist limit");
    expect(voice.setParameters(parameters),
           "basic VA voice restores settings after rejecting Nyquist pitch");

    expect(voice.render(output.data(), SynthVoiceRack::MaxRenderFrames, 48000) &&
               voice.render(output.data() + SynthVoiceRack::MaxRenderFrames,
                            output.size() - SynthVoiceRack::MaxRenderFrames,
                            48000) &&
               voice.isActive(),
           "basic VA oscillator sustains until its external VCA gates it");
    expect(voice.noteOn(1.0F) &&
               voice.render(output.data(), 32, 48000) &&
               voice.isActive(),
           "basic VA voice can retrigger with the shared envelope");
    voice.noteOff();
    expect(!voice.isActive(),
           "basic VA note-off immediately silences oscillator output");
}

void testSelectableSynthVoice() {
    using namespace pocket;
    using namespace pocket::audio;

    SelectableSynthVoice voice;
    SynthParameters parameters;
    parameters.pitchHz = 220.0F;
    parameters.level = 0.8F;
    parameters.timbre = 0.6F;
    parameters.color = 0.6F;
    parameters.engine = SynthEngineType::BasicVa;
    expect(voice.setParameters(parameters) &&
               voice.engineType() == SynthEngineType::BasicVa,
           "selectable voice accepts the basic engine parameter set");

    std::array<float, 256> output{};
    expect(voice.noteOn(0.8F) &&
               voice.render(output.data(), output.size(), 48000),
           "selectable voice renders the basic engine");
    voice.noteOff();

    for (int engine = 0; engine < 16; ++engine) {
        parameters.model = static_cast<std::uint8_t>(engine);
        parameters.morph = 0.5F;
        parameters.engine = SynthEngineType::Plaits;
        if (!voice.setParameters(parameters) || !voice.noteOn(1.0F)) {
            expect(false, "Plaits engine accepts parameters and triggers");
            break;
        }
        bool audible = false;
        bool bounded = true;
        for (std::size_t frame = 0; frame < output.size(); ++frame) {
            output[frame] = 0.0F;
        }
        if (!voice.render(output.data(), output.size(), 48000)) {
            expect(false, "Plaits engine renders a mono block");
            break;
        }
        for (const auto sample : output) {
            audible = audible || std::abs(sample) > 1.0e-6F;
            bounded = bounded && std::isfinite(sample) &&
                      std::abs(sample) <= 1.0F;
        }
        expect(audible && bounded,
               "Plaits engine " + std::to_string(engine) +
                   " is audible, finite, and bounded");
        voice.noteOff();
    }

    parameters.model = 16;
    parameters.engine = SynthEngineType::Plaits;
    expect(!voice.setParameters(parameters),
           "Plaits voice rejects an out-of-range model index");

    for (std::size_t model = 0; model < PlaitsModelCount; ++model) {
        const std::string name = plaitsModelName(model);
        expect(!name.empty() && name.size() <= 4,
               "Plaits model names fit the SOUND value column without shrinking");
    }
}

void testSynthEngineParameterRouting() {
    using namespace pocket;
    using namespace pocket::audio;

    Sequencer sequencer;
    SamplerVoicePool samplerVoices;
    SynthVoiceRack rack;
    std::array<SelectableSynthVoice, TrackCount> voices{};
    SynthParameters parameters;
    parameters.level = 0.5F;
    for (std::size_t track = 0; track < voices.size(); ++track) {
        expect(rack.configure(track, &voices[track], parameters),
               "prepare selectable voice for each track");
    }
    Mixer mixer;
    SamplerMixerRenderer renderer(sequencer, samplerVoices, rack, mixer, 100);
    expect(renderer.applyCommand(
               {AudioParameter::TrackSynthEngineType, 0, 1.0F}, 48000),
           "engine type command is accepted by the renderer");
    expect(rack.parameters(0).engine == SynthEngineType::Plaits,
           "engine type command switches the rack voice to Plaits");
    expect(renderer.applyCommand(
               {AudioParameter::TrackSynthEngineType, 0, 0.0F}, 48000) &&
               rack.parameters(0).engine == SynthEngineType::BasicVa,
           "engine type command switches the rack voice back to BasicVA");
    expect(!renderer.applyCommand(
               {AudioParameter::TrackSynthEngineType, 0, 2.0F}, 48000),
           "engine type command rejects out-of-range values");
}

void testOnePoleLowPass() {
    using namespace pocket::audio;
    OnePoleLowPass filter;
    expect(!filter.setCutoff(99.0F, 48000) &&
               !filter.setCutoff(1000.0F, 7999) &&
               filter.setCutoff(100.0F, 48000),
           "one-pole filter validates bounded cutoff and sample rate");

    float output = 0.0F;
    for (std::size_t frame = 0; frame < 512; ++frame) {
        output = filter.process(frame == 0 ? 1.0F : 0.0F);
        expect(std::isfinite(output) && output >= 0.0F && output <= 1.0F,
               "one-pole filter maintains a finite bounded impulse response");
    }
    expect(output < 0.01F,
           "low-pass impulse response remains stable and decays toward silence");
    filter.reset();
    for (std::size_t frame = 0; frame < 256; ++frame) {
        output = filter.process((frame & 1U) == 0 ? 1.0F : -1.0F);
    }
    expect(std::abs(output) < 0.02F,
           "low cutoff attenuates a sustained high-frequency alternating signal");

    OnePoleLowPass nyquistLimited;
    expect(nyquistLimited.setCutoff(20000.0F, 8000),
           "filter accepts supported cutoff independently of device rate");

    pocket::Sequencer sequencer;
    pocket::TrackFilterSettings settings;
    settings.enabled = true;
    settings.cutoffHz = 2500.0F;
    expect(sequencer.setFilter(5, settings) &&
               sequencer.track(5).filter.enabled &&
               sequencer.track(5).filter.cutoffHz == 2500.0F,
           "sequencer stores filter settings independently per track");
    settings.cutoffHz = std::numeric_limits<float>::quiet_NaN();
    expect(!sequencer.setFilter(5, settings) &&
               sequencer.track(5).filter.cutoffHz == 2500.0F,
           "sequencer rejects invalid filter settings without partial updates");
}

void testAudioParameterQueue() {
    using namespace pocket::audio;
    AudioParameterQueue queue;
    expect(queue.isLockFree(),
           "audio parameter queue exposes lock-free preflight state");
    expect(!queue.push({AudioParameter::TrackPan, 0, 1.1F}) &&
               !queue.push({AudioParameter::TrackDecay, 0, 0.0F}) &&
               !queue.push({AudioParameter::TrackReverbSend, 0, 1.1F}) &&
               !queue.push({AudioParameter::TrackDelaySend, 0, -0.1F}) &&
               !queue.push({AudioParameter::TrackVolume, 8, 50.0F}),
           "audio parameter queue rejects invalid values and track indices");

    for (std::size_t index = 0; index < AudioParameterQueue::Capacity; ++index) {
        expect(queue.push({AudioParameter::TrackVolume, 0,
                           static_cast<float>(index % 101)}),
               "audio parameter queue accepts commands up to its fixed capacity");
    }
    expect(!queue.push({AudioParameter::MasterVolume, 0, 50.0F}),
           "audio parameter queue reports full without overwriting commands");
    AudioParameterCommand command;
    bool ordered = true;
    for (std::size_t index = 0; index < AudioParameterQueue::Capacity; ++index) {
        ordered = queue.pop(command) && ordered &&
                  command.parameter == AudioParameter::TrackVolume &&
                  command.value == static_cast<float>(index % 101);
    }
    expect(ordered && !queue.pop(command),
           "audio parameter queue preserves FIFO ordering and emptiness");
}

void testSequencerCommandSync() {
    using namespace pocket;
    using namespace pocket::audio;
    Sequencer ui;
    Sequencer audioSide = ui;
    SequencerCommandQueue queue;
    SequencerSync sync(queue, ui);
    expect(queue.isLockFree(), "sequencer command queue is lock-free");

    SequencerCommand none;
    sync.sync(ui);
    expect(!queue.pop(none), "unchanged sequencer produces no commands");

    ui.setRhythm(2, {12, 5, 1});
    ui.setMuted(3, true);
    ui.setVolume(4, 55);
    ui.setPan(4, -0.5F);
    ui.setReverbSend(6, 0.4F);
    ui.setDelaySend(7, 0.7F);
    auto synth = ui.track(5).synth;
    synth.engineType = SynthEngineType::Plaits;
    synth.morph = 0.75F;
    ui.setSynth(5, synth);
    ui.setTempo(140.0);
    ui.start();
    sync.sync(ui);
    sync.sync(ui);
    SequencerCommand command;
    while (queue.pop(command)) {
        applySequencerCommand(audioSide, command);
    }
    expect(audioSide.track(2).rhythm.length == 12 &&
               audioSide.track(2).rhythm.fill == 5 &&
               audioSide.track(2).rhythm.offset == 1 &&
               audioSide.track(3).muted && audioSide.track(4).volume == 55 &&
               audioSide.track(4).pan == -0.5F && audioSide.tempo() == 140.0 &&
               audioSide.track(6).reverbSend == 0.4F &&
               audioSide.track(7).delaySend == 0.7F &&
               audioSide.track(5).synth.engineType ==
                   SynthEngineType::Plaits &&
               audioSide.track(5).synth.morph == 0.75F &&
               audioSide.isPlaying(),
           "synced commands reproduce UI edits on the audio-side sequencer");

    ui.stop();
    sync.sync(ui);
    expect(queue.pop(command) && command.type == SequencerCommandType::Stop &&
               !queue.pop(command),
           "only the changed transport state is sent");

    // Overflow: unsent changes stay pending and are retried.
    for (std::size_t round = 0; round < 40; ++round) {
        for (std::size_t t = 0; t < TrackCount; ++t) {
            ui.setVolume(t, static_cast<std::uint8_t>(10 + round));
        }
        sync.sync(ui);
    }
    ui.setVolume(7, 99);
    while (queue.pop(command)) {
        applySequencerCommand(audioSide, command);
    }
    sync.sync(ui);
    while (queue.pop(command)) {
        applySequencerCommand(audioSide, command);
    }
    sync.sync(ui);
    while (queue.pop(command)) {
        applySequencerCommand(audioSide, command);
    }
    bool converged = true;
    for (std::size_t t = 0; t < TrackCount; ++t) {
        converged = converged && audioSide.track(t).volume == ui.track(t).volume;
    }
    expect(converged, "dropped sync commands converge after the queue drains");
}

void testWavDecoder() {
    using pocket::audio::WavDecodeError;
    using pocket::audio::WavDecoder;
    static_assert(WavDecoder::MaxDecodedBytes == 64U * 1024U * 1024U);
    static_assert(WavDecoder::MaxDecodedFrames ==
                  WavDecoder::MaxDecodedBytes / sizeof(float));

    std::array<float, 8> decoded{};
    const auto pcm16 = makeWave(
        1, 1, 48000, 16,
        {0x00, 0x80, 0xff, 0x7f, 0x00, 0x00}, false, true);
    const auto pcm16Result = WavDecoder::decodeMono(
        pcm16.data(), pcm16.size(), decoded.data(), decoded.size());
    expect(pcm16Result && pcm16Result.frameCount == 3 &&
               pcm16Result.sampleRate == 48000 &&
               decoded[0] == -1.0F && decoded[1] > 0.999F &&
               decoded[1] < 1.0F && decoded[2] == 0.0F,
           "WAV decoder handles PCM16 and padded unknown RIFF chunks");

    const auto pcm8 =
        makeWave(1, 1, 22050, 8, {0x00, 0x80, 0xff});
    const auto pcm8Result = WavDecoder::decodeMono(
        pcm8.data(), pcm8.size(), decoded.data(), decoded.size());
    expect(pcm8Result && decoded[0] == -1.0F && decoded[1] == 0.0F &&
               decoded[2] > 0.99F && decoded[2] < 1.0F,
           "WAV decoder converts unsigned PCM8 to normalized mono floats");

    const auto pcm24 = makeWave(
        1, 1, 44100, 24,
        {0x00, 0x00, 0x80, 0xff, 0xff, 0x7f});
    const auto pcm24Result = WavDecoder::decodeMono(
        pcm24.data(), pcm24.size(), decoded.data(), decoded.size());
    expect(pcm24Result && decoded[0] == -1.0F && decoded[1] > 0.999F,
           "WAV decoder sign-extends PCM24 without changing polarity");

    const auto pcm32 = makeWave(
        1, 1, 48000, 32,
        {0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0x7f});
    const auto pcm32Result = WavDecoder::decodeMono(
        pcm32.data(), pcm32.size(), decoded.data(), decoded.size());
    expect(pcm32Result && decoded[0] == -1.0F && decoded[1] > 0.999F,
           "WAV decoder converts signed PCM32 to normalized floats");

    const auto stereo = makeWave(
        1, 2, 48000, 16,
        {0x00, 0x40, 0x00, 0xc0, 0x00, 0x80, 0x00, 0x80});
    const auto stereoResult = WavDecoder::decodeMono(
        stereo.data(), stereo.size(), decoded.data(), decoded.size());
    expect(stereoResult && stereoResult.frameCount == 2 &&
               std::abs(decoded[0]) < 1.0e-6F && decoded[1] == -1.0F,
           "WAV decoder averages stereo PCM channels into mono");

    std::uint32_t floatBits = 0;
    const float half = 0.5F;
    std::memcpy(&floatBits, &half, sizeof(half));
    std::vector<std::uint8_t> floatData;
    append32(floatData, floatBits);
    const auto floatWav = makeWave(3, 1, 48000, 32, floatData, true);
    const auto floatResult = WavDecoder::decodeMono(
        floatWav.data(), floatWav.size(), decoded.data(), decoded.size());
    expect(floatResult && decoded[0] == half,
           "WAV decoder handles IEEE float32 WAVEFORMATEXTENSIBLE");
    const auto integerExtensible =
        makeWave(1, 1, 48000, 16, {0x00, 0x40}, true);
    expect(WavDecoder::decodeMono(
               integerExtensible.data(), integerExtensible.size(),
               decoded.data(), decoded.size()) &&
               decoded[0] == 0.5F,
           "WAV decoder handles PCM WAVEFORMATEXTENSIBLE");

    const auto clampedFloat = [] {
        std::vector<std::uint8_t> bytes;
        const float value = 2.0F;
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(value));
        append32(bytes, bits);
        return makeWave(3, 1, 48000, 32, bytes);
    }();
    expect(WavDecoder::decodeMono(
               clampedFloat.data(), clampedFloat.size(), decoded.data(),
               decoded.size()) &&
               decoded[0] == 1.0F,
           "WAV decoder clamps finite float PCM to the normalized audio range");

    auto badContainer = pcm16;
    badContainer[0] = 'X';
    expect(WavDecoder::decodeMono(badContainer.data(), badContainer.size(),
                                  decoded.data(), decoded.size())
                   .error == WavDecodeError::InvalidContainer,
           "WAV decoder rejects non-RIFF input");
    expect(WavDecoder::decodeMono(nullptr, pcm16.size(), decoded.data(),
                                  decoded.size())
                   .error == WavDecodeError::InvalidArgument &&
               WavDecoder::decodeMono(pcm16.data(), pcm16.size(), nullptr,
                                      decoded.size())
                       .error == WavDecodeError::InvalidArgument,
           "WAV decoder rejects missing input and output buffers");

    const auto unsupported = makeWave(1, 1, 48000, 12, {0, 0});
    expect(WavDecoder::decodeMono(unsupported.data(), unsupported.size(),
                                  decoded.data(), decoded.size())
                   .error == WavDecodeError::UnsupportedFormat,
           "WAV decoder rejects unsupported sample bit depths");
    auto invalidFormat = pcm16;
    invalidFormat[28] = 0;
    invalidFormat[29] = 0;
    invalidFormat[30] = 0;
    invalidFormat[31] = 0;
    expect(WavDecoder::decodeMono(invalidFormat.data(), invalidFormat.size(),
                                  decoded.data(), decoded.size())
                   .error == WavDecodeError::InvalidFormat,
           "WAV decoder rejects inconsistent format byte rate");

    std::array<float, 2> tooSmall{{9.0F, 9.0F}};
    expect(WavDecoder::decodeMono(pcm16.data(), pcm16.size(),
                                  tooSmall.data(), tooSmall.size())
                       .error == WavDecodeError::OutputTooSmall &&
               tooSmall[0] == 9.0F && tooSmall[1] == 9.0F,
           "WAV decoder checks output capacity before writing samples");

    std::uint32_t nanBits = 0x7fc00000U;
    std::vector<std::uint8_t> nonFiniteData;
    append32(nonFiniteData, nanBits);
    const auto nonFiniteWav =
        makeWave(3, 1, 48000, 32, nonFiniteData);
    std::array<float, 2> unchanged{{7.0F, 8.0F}};
    expect(WavDecoder::decodeMono(
               nonFiniteWav.data(), nonFiniteWav.size(), unchanged.data(),
               unchanged.size())
                       .error == WavDecodeError::NonFiniteSample &&
               unchanged[0] == 7.0F && unchanged[1] == 8.0F,
           "WAV decoder rejects non-finite samples without partial output");

    auto truncated = pcm16;
    truncated[4] = 0xff;
    truncated[5] = 0xff;
    truncated[6] = 0xff;
    truncated[7] = 0x7f;
    expect(WavDecoder::decodeMono(truncated.data(), truncated.size(),
                                  decoded.data(), decoded.size())
                   .error == WavDecodeError::InvalidContainer,
           "WAV decoder rejects RIFF sizes beyond the provided file buffer");
}

void testSampleCache() {
    using namespace pocket::audio;
    std::array<float, 8> storage;
    storage.fill(-9.0F);
    SampleCache cache(storage.data(), storage.size());
    const auto firstWave = makeWave(1, 1, 48000, 16,
                                    {0x00, 0x40, 0x00, 0xc0});
    const auto secondWave = makeWave(1, 1, 22050, 8,
                                     {0x00, 0x80, 0xff});

    expect(cache.isValid() && cache.capacityFrames() == storage.size(),
           "sample cache accepts caller-owned bounded PCM storage");
    const auto first = cache.loadWave(0, firstWave.data(), firstWave.size());
    expect(first && first.frameCount == 2 && first.sampleRate == 48000 &&
               cache.usedFrames() == 2 && cache.usedBytes() == 2 * sizeof(float),
           "sample cache decodes a WAV directly into its fixed storage");
    MonoSampleView view;
    expect(cache.sample(0, view) && view.frameCount == 2 &&
               view.sampleRate == 48000 && view.samples == storage.data() &&
               view.samples[0] == 0.5F && view.samples[1] == -0.5F,
           "sample cache exposes immutable per-track views");

    const auto duplicate = cache.loadWave(0, secondWave.data(), secondWave.size());
    expect(duplicate.error == SampleCacheError::AlreadyLoaded &&
               cache.usedFrames() == 2 && view.samples[0] == 0.5F,
           "sample cache leaves existing track data intact on duplicate load");
    const auto invalid = cache.loadWave(1, nullptr, 0);
    expect(invalid.error == SampleCacheError::DecodeFailed &&
               invalid.decodeError == WavDecodeError::InvalidArgument &&
               cache.usedFrames() == 2,
           "sample cache reports WAV errors without reserving storage");

    const auto second = cache.loadWave(1, secondWave.data(), secondWave.size());
    expect(second && second.frameCount == 3 && cache.usedFrames() == 5 &&
               cache.sample(1, view) && view.samples == storage.data() + 2 &&
               view.samples[0] == -1.0F && view.samples[1] == 0.0F,
           "sample cache places a second track in the next free region");

    const auto thirdWave = makeWave(
        1, 1, 48000, 16,
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    const auto noSpace = cache.loadWave(
        2, thirdWave.data(), thirdWave.size());
    expect(noSpace.error == SampleCacheError::NoContiguousSpace &&
               storage[0] == 0.5F && storage[1] == -0.5F &&
               storage[2] == -1.0F && storage[3] == 0.0F &&
               cache.usedFrames() == 5,
           "cache refuses oversized samples without modifying occupied regions");
    expect(cache.unload(0) && cache.usedFrames() == 3 &&
               !cache.sample(0, view) && !cache.unload(0),
           "unloading frees only the selected track sample");
    const auto reused = cache.loadWave(2, firstWave.data(), firstWave.size());
    expect(reused && reused.frameCount == 2 && cache.usedFrames() == 5 &&
               cache.sample(2, view) && view.samples == storage.data(),
           "first-fit allocator reuses a freed contiguous region");
    expect(cache.unload(1) && cache.unload(2),
           "cache releases adjacent track allocations");
    const auto coalesced = cache.loadWave(
        2, thirdWave.data(), thirdWave.size());
    expect(coalesced && coalesced.frameCount == 5 &&
               cache.usedFrames() == 5 && cache.usedBytes() == 5 * sizeof(float),
           "cache reuses coalesced capacity without exceeding its total budget");

    SampleCache invalidStorage(nullptr, 8);
    expect(!invalidStorage.isValid() &&
               invalidStorage.loadWave(0, firstWave.data(), firstWave.size())
                       .error == SampleCacheError::InvalidStorage,
           "sample cache reports missing caller storage explicitly");
    SampleCache tooLargeStorage(
        storage.data(), SampleCache::MaxCacheFrames + 1);
    expect(!tooLargeStorage.isValid(),
           "sample cache rejects storage exceeding the 64 MiB budget");
    expect(cache.loadWave(pocket::TrackCount, firstWave.data(),
                          firstWave.size())
                   .error == SampleCacheError::TrackOutOfRange,
           "sample cache rejects invalid track indices");
}

void testVersionedSettingsAndProjectSeparation() {
    const pocket::platform::SystemSettings settings;
    expect(pocket::platform::isValid(settings),
           "default versioned system settings are valid");
    expect(settings.display.width == 1024 &&
               settings.display.height == 768 &&
               settings.display.theme == pocket::platform::ThemeId::QuietDark,
           "default display profile matches the Brick Pro design resolution");
    const auto* quietDark = pocket::ui::paletteFor(settings.display.theme);
    const auto* warmPaper =
        pocket::ui::paletteFor(pocket::platform::ThemeId::WarmPaper);
    const auto* highContrast =
        pocket::ui::paletteFor(pocket::platform::ThemeId::HighContrast);
    bool themeCatalogValid =
        pocket::ui::themeCount() >= 8 &&
        pocket::ui::themeAt(pocket::ui::themeCount()) == nullptr;
    for (std::size_t index = 0; index < pocket::ui::themeCount(); ++index) {
        const auto* option = pocket::ui::themeAt(index);
        themeCatalogValid =
            themeCatalogValid && option != nullptr &&
            !option->name.empty() &&
            pocket::ui::themeFor(option->id) == option;
    }
    expect(quietDark != nullptr && warmPaper != nullptr &&
               highContrast != nullptr &&
               themeCatalogValid &&
               quietDark->background.red != warmPaper->background.red &&
               quietDark->primary.green != warmPaper->primary.green &&
               highContrast->primary.red == 255,
           "display themes resolve to distinct built-in semantic color palettes");
    auto selectedThemeSettings = settings;
    selectedThemeSettings.display.theme =
        pocket::platform::ThemeId::WarmPaper;
    expect(pocket::platform::isValid(selectedThemeSettings) &&
               pocket::ui::paletteFor(selectedThemeSettings.display.theme) ==
                   warmPaper,
           "global display settings select a palette independently of projects");

    auto invalidSettings = settings;
    invalidSettings.formatVersion++;
    expect(!pocket::platform::isValid(invalidSettings),
           "reject unsupported system settings format version");
    invalidSettings = settings;
    invalidSettings.midi.outputChannel = 17;
    expect(!pocket::platform::isValid(invalidSettings),
           "validate MIDI channel range in system settings");
    invalidSettings = settings;
    invalidSettings.audio.masterVolumePercent = 101;
    expect(!pocket::platform::isValid(invalidSettings),
           "validate globally persisted master output volume");
    invalidSettings = settings;
    invalidSettings.display.theme = static_cast<pocket::platform::ThemeId>(255);
    expect(!pocket::platform::isValid(invalidSettings) &&
               pocket::ui::paletteFor(invalidSettings.display.theme) == nullptr,
           "reject unknown theme identifiers instead of silently selecting a palette");

    const pocket::platform::SystemCapabilities handheldCapabilities{
        false, false, true, false, true, true};
    expect(!pocket::platform::isCategoryAvailable(
               pocket::platform::SettingsCategory::Midi,
               handheldCapabilities),
           "hide system categories unavailable on the current platform");
    expect(pocket::platform::isCategoryAvailable(
               pocket::platform::SettingsCategory::InputAndControls,
               handheldCapabilities),
           "show system categories supported by the current platform");

    const pocket::project::ProjectData project;
    expect(project.formatVersion == pocket::project::ProjectFormatVersion,
           "project data has its own format version");
    expect(project.tracks[0].lfos.size() == pocket::MaxLfosPerTrack,
           "project data can store every supported per-track LFO slot");
    expect(project.tempoBpm == settings.clock.initialTempoBpm,
           "project tempo and system initial tempo have independent storage");
    expect(!project.tracks[0].lfos[0].enabled,
           "project track data stores modulation configuration, not runtime phase");
}

void testLiveChangesMuteAndTempo() {
    pocket::Sequencer sequencer;
    sequencer.start();
    expect(sequencer.setRhythm(0, {5, 1, 0}), "configure live-change track");
    sequencer.tick();
    sequencer.tick();
    expect(sequencer.track(0).position == 2, "position advances before live edit");
    expect(sequencer.setRhythm(0, {2, 1, 1}), "accept live rhythm edit");
    expect(sequencer.track(0).position == 0,
           "live length edit immediately wraps position into new length");

    expect(sequencer.setRhythm(0, {2, 2, 0}),
           "make each step active for mixer mute timing checks");
    expect(sequencer.setMuted(0, true), "mute track");
    expect(sequencer.tick().triggered[0],
           "sequencer keeps generating triggers while mixer mute is active");
    expect(sequencer.setMuted(0, false), "unmute track");
    expect(sequencer.tick().triggered[0], "unmuted track resumes triggering");
    expect(!sequencer.setTempo(39.0), "reject tempo below range");
    expect(!sequencer.setTempo(241.0), "reject tempo above range");
    expect(!sequencer.setTempo(std::numeric_limits<double>::quiet_NaN()),
           "reject non-finite tempo");
    expect(sequencer.setTempo(120.0), "accept tempo within range");
    expect(sequencer.tempo() == 120.0, "tempo value is retained");

    sequencer.reset();
    expect(sequencer.track(0).position == 0 &&
               sequencer.masterTickPosition() == 0,
           "explicit reset returns tracks and master position to zero");
}

void testSampleTimeScheduling() {
    pocket::Sequencer sequencer;
    expect(sequencer.setRhythm(0, {32, 32, 0}), "configure clock test track");
    sequencer.start();

    std::vector<std::uint32_t> offsets;
    std::vector<std::uint64_t> tickPositions;
    const bool validRate = sequencer.processFrames(
        48000, 48000,
        [&offsets, &tickPositions](std::uint32_t offset,
                                   const pocket::TickResult& tick) {
            offsets.push_back(offset);
            tickPositions.push_back(tick.masterTickPosition);
        });
    expect(validRate, "accept non-zero sample rate");
    expect(offsets == std::vector<std::uint32_t>(
                          {0, 6000, 12000, 18000, 24000, 30000, 36000, 42000}),
           "120 BPM produces eight 125 ms sixteenth-note ticks per second");
    expect(tickPositions == std::vector<std::uint64_t>({0, 1, 2, 3, 4, 5, 6, 7}),
           "scheduled tick events carry sequential master-clock positions");
    expect(sequencer.masterTickPosition() == 8,
           "master-clock position advances once per scheduled tick");

    offsets.clear();
    tickPositions.clear();
    sequencer.stop();
    expect(sequencer.processFrames(
               12000, 48000,
               [&offsets](std::uint32_t offset, const pocket::TickResult&) {
                   offsets.push_back(offset);
               }),
           "stopped transport accepts audio blocks");
    expect(offsets.empty(), "stopped transport schedules no ticks");

    sequencer.start();
    expect(sequencer.processFrames(
               6000, 48000,
               [&offsets, &tickPositions](std::uint32_t offset,
                                          const pocket::TickResult& tick) {
                   offsets.push_back(offset);
                   tickPositions.push_back(tick.masterTickPosition);
               }),
           "restarted transport accepts audio blocks");
    expect(offsets == std::vector<std::uint32_t>({0}),
           "transport starts its synchronized clock at the beginning of the block");
    expect(tickPositions == std::vector<std::uint64_t>({0}) &&
               sequencer.masterTickPosition() == 1,
           "START resets clock phase before emitting the step-zero tick");
    expect(!sequencer.processFrames(
               1, 0,
               [](std::uint32_t, const pocket::TickResult&) {}),
           "reject zero sample rate");
}

void testAudioCallbackProcessor() {
    pocket::Sequencer sequencer;
    pocket::ui::StepTriggerEventQueue visualEvents;
    pocket::audio::AudioCallbackProcessor processor(sequencer, visualEvents);
    pocket::audio::AudioParameterQueue unattachedQueue;
    processor.setParameterQueue(&unattachedQueue);
    expect(!processor.isRealtimeSafe(),
           "callback preflight rejects queued controls without a renderer");
    processor.setParameterQueue(nullptr);
    expect(sequencer.setRhythm(0, {32, 32, 0}),
           "configure callback processor trigger track");
    for (std::size_t track = 1; track < pocket::TrackCount; ++track) {
        expect(sequencer.setRhythm(track, {2, 1, 1}),
               "keep unrelated tracks silent at the first callback tick");
    }
    sequencer.start();
    std::array<float, 12000> output;
    output.fill(1.0F);
    expect(processor.process(output.data(), 4096, 0, 48000),
           "audio callback processor schedules one valid output block");
    expect(sequencer.masterTickPosition() == 1,
           "audio callback sends the first trigger at sample offset zero");
    expect(processor.process(output.data() + 8192, 1904, 4096, 48000),
           "callback accepts the remaining frames in a split audio interval");
    bool outputCleared = true;
    for (float sample : output) {
        outputCleared = outputCleared && sample == 0.0F;
    }
    expect(outputCleared, "silent initial engine fully initializes stereo output");

    pocket::ui::StepTriggerEvent event;
    expect(visualEvents.pop(event),
           "audio callback publishes a trigger event");
    expect(event.audioFrame == 0 && event.masterTickPosition == 0 &&
               event.triggeredMask == 0x01 && event.stepPositions[0] == 0,
           "audio callback publishes the matching absolute trigger event");
    expect(!visualEvents.pop(event),
           "non-triggering callback intervals do not publish light events");

    std::array<float, 12000> followupOutput;
    followupOutput.fill(1.0F);
    expect(processor.process(followupOutput.data(), 4096, 6000, 48000) &&
               sequencer.masterTickPosition() == 2,
           "callback block boundaries retain sample-accurate tick phase");
    expect(visualEvents.pop(event) && event.audioFrame == 6000 &&
               event.masterTickPosition == 1,
           "successive callback blocks use the absolute audio frame timeline");
    expect(processor.process(followupOutput.data() + 8192, 1904, 10096,
                             48000) &&
               sequencer.masterTickPosition() == 2,
           "split callback interval ends on the next tick boundary");

    expect(!processor.process(followupOutput.data(), 128, 12000, 0),
           "callback rejects invalid sample rate");
    bool invalidRateOutputCleared = true;
    for (std::size_t sample = 0; sample < 256; ++sample) {
        invalidRateOutputCleared = invalidRateOutputCleared &&
                                   followupOutput[sample] == 0.0F;
    }
    expect(invalidRateOutputCleared,
           "invalid audio spec still produces initialized silent output");
    expect(processor.process(followupOutput.data(), 128, 12000, 48000) &&
               sequencer.masterTickPosition() == 3 &&
               visualEvents.pop(event) && event.audioFrame == 12000,
           "valid callback continues at the next contiguous block boundary");
    expect(!processor.process(followupOutput.data(), 4097, 12128, 48000) &&
               !processor.process(nullptr, 128, 12128, 48000),
           "callback rejects oversized blocks and missing output buffers");
    expect(!processor.process(followupOutput.data(), 128, 12129, 48000),
           "callback rejects discontinuous absolute frame timelines");
    pocket::audio::AudioCallbackProcessor::callback(
        &processor, followupOutput.data(), 128, 12128, 0);
    expect(processor.callbackFailureCount() == 1,
           "platform callback reports invalid blocks through diagnostics counter");
    processor.resetTimeline();
    expect(processor.callbackFailureCount() == 0 &&
               processor.process(followupOutput.data(), 128, 0, 48000),
           "stopped callback timeline can be reset for a new device open");
}

void testSamplerMixerAudioPipeline() {
    pocket::Sequencer sequencer;
    pocket::ui::StepTriggerEventQueue visualEvents;
    pocket::audio::AudioParameterQueue parameterQueue;
    pocket::audio::SamplerVoicePool voices;
    pocket::audio::Mixer mixer;
    std::array<float, 20000> sample{};
    sample.fill(0.5F);
    expect(sequencer.setMuted(0, true) &&
               voices.assignSample(0, {sample.data(), sample.size(), 48000}) &&
               sequencer.setRhythm(0, {2, 2, 0}),
           "prepare sampler audio pipeline sample and two-step rhythm");

    pocket::audio::SamplerMixerRenderer renderer(
        sequencer, voices, mixer, 100);
    expect(renderer.setMasterVolume(100) && !renderer.setMasterVolume(101),
           "sampler renderer validates global master volume");
    pocket::audio::AudioCallbackProcessor processor(
        sequencer, visualEvents, renderer);
    processor.setParameterQueue(&parameterQueue);
    expect(parameterQueue.push(
               {pocket::audio::AudioParameter::TrackMute, 0, 0.0F}),
           "queue unmute for the audio-owned mixer state");
    expect(processor.isRealtimeSafe(),
           "sampler audio pipeline uses lock-free callback diagnostics");

    sequencer.start();
    std::array<float, 8192> firstBlock{};
    expect(processor.process(firstBlock.data(), 4096, 0, 48000),
           "sampler audio pipeline processes a bounded first callback block");
    expect(voices.activeVoiceCount(0) == 1 &&
               mixer.meters().track(0) > 0.0F &&
               mixer.meters().master() > 0.0F,
           "first sequencer event triggers a sample routed through mixer and meters");
    expect(firstBlock[0] < firstBlock[200] &&
               firstBlock[1] < firstBlock[201] &&
               firstBlock[200] > 0.0F && firstBlock[201] > 0.0F,
           "rendered sampler audio passes through its per-track AD VCA and mixer");

    std::array<float, 8192> secondBlock{};
    expect(processor.process(secondBlock.data(), 4096, 4096, 48000) &&
               voices.activeVoiceCount(0) == 2,
           "sample-accurate tick inside a block starts an overlapping voice");
    expect(secondBlock[0] > 0.0F &&
               secondBlock[2 * 1903] > 0.0F &&
               secondBlock[2 * 1904] > 0.0F,
           "audio before and after an in-block tick remains rendered continuously");
    pocket::ui::StepTriggerEvent event;
    expect(visualEvents.pop(event) && event.audioFrame == 0 &&
               visualEvents.pop(event) && event.audioFrame == 6000,
           "pipeline preserves absolute event frames at audio block boundaries");

    expect(parameterQueue.push(
               {pocket::audio::AudioParameter::TrackMute, 0, 1.0F}),
           "queue mute for sampler track without stopping voice progression");
    std::array<float, 8192> mutedBlock{};
    expect(processor.process(mutedBlock.data(), 4096, 8192, 48000),
           "audio callback applies queued mute without blocking");
    expect(mutedBlock.back() < secondBlock[2 * 1903] &&
               voices.activeVoiceCount(0) == 3,
           "mixer mute attenuates output while sampler voices keep running");

    pocket::Sequencer envelopeSequencer;
    pocket::audio::SamplerVoicePool envelopeVoices;
    pocket::audio::Mixer envelopeMixer;
    pocket::ui::StepTriggerEventQueue envelopeEvents;
    pocket::AdEnvelopeSettings shortEnvelope;
    shortEnvelope.attackSeconds = 0.001F;
    shortEnvelope.decaySeconds = 0.02F;
    expect(envelopeSequencer.setRhythm(0, {2, 2, 0}) &&
               envelopeSequencer.setEnvelope(0, shortEnvelope) &&
               envelopeVoices.assignSample(
                   0, {sample.data(), sample.size(), 48000}),
           "prepare a short per-track VCA envelope over a long sample");
    pocket::audio::SamplerMixerRenderer envelopeRenderer(
        envelopeSequencer, envelopeVoices, envelopeMixer);
    pocket::audio::AudioCallbackProcessor envelopeProcessor(
        envelopeSequencer, envelopeEvents, envelopeRenderer);
    envelopeSequencer.start();
    std::array<float, 8192> envelopeBlock{};
    expect(envelopeProcessor.process(envelopeBlock.data(), 4096, 0, 48000) &&
               envelopeVoices.activeVoiceCount(0) == 0 &&
               envelopeBlock[2 * 1500] == 0.0F &&
               envelopeBlock[2 * 1500 + 1] == 0.0F,
           "per-track VCA decay stops a long sample and leaves the block silent");
}

void testLiveAudioParameterHandoff() {
    pocket::Sequencer sequencer;
    pocket::ui::StepTriggerEventQueue visualEvents;
    pocket::audio::AudioParameterQueue parameterQueue;
    pocket::audio::SamplerVoicePool voices;
    pocket::audio::Mixer mixer;
    std::array<float, 30000> sample{};
    sample.fill(0.5F);
    expect(sequencer.setRhythm(0, {2, 2, 0}) &&
               voices.assignSample(0, {sample.data(), sample.size(), 48000}),
           "prepare live parameter pipeline with a long sample");

    pocket::audio::SamplerMixerRenderer renderer(sequencer, voices, mixer);
    pocket::audio::AudioCallbackProcessor processor(
        sequencer, visualEvents, renderer);
    processor.setParameterQueue(&parameterQueue);
    expect(processor.isRealtimeSafe(),
           "audio callback preflights the parameter queue atomics");
    expect(parameterQueue.push(
               {pocket::audio::AudioParameter::TrackDecay, 0, 0.02F}) &&
               parameterQueue.push(
                   {pocket::audio::AudioParameter::TrackFilterEnabled, 0,
                    1.0F}) &&
               parameterQueue.push(
                   {pocket::audio::AudioParameter::TrackFilterCutoff, 0,
                    1000.0F}),
           "UI-side producer queues live envelope and filter updates");

    sequencer.start();
    std::array<float, 8192> firstBlock{};
    expect(processor.process(firstBlock.data(), 4096, 0, 48000) &&
               voices.activeVoiceCount(0) == 0,
           "audio callback applies queued settings before the scheduled trigger");
    bool silentTail = true;
    for (std::size_t frame = 3000; frame < 4096; ++frame) {
        silentTail = silentTail && firstBlock[frame * 2] == 0.0F &&
                     firstBlock[frame * 2 + 1] == 0.0F;
    }
    expect(silentTail,
           "queued short decay gates the long sample during the same callback");

    expect(parameterQueue.push(
               {pocket::audio::AudioParameter::TrackMute, 0, 1.0F}),
           "live mute can be queued from the control thread");
    std::array<float, 512> secondBlock{};
    expect(processor.process(secondBlock.data(), 256, 4096, 48000),
           "audio callback drains live mixer commands at a block boundary");
}

void testSynthMixerAudioPipeline() {
    pocket::Sequencer sequencer;
    pocket::ui::StepTriggerEventQueue visualEvents;
    pocket::audio::SamplerVoicePool samplerVoices;
    pocket::audio::SynthVoiceRack synthVoices;
    std::array<pocket::audio::BasicVaVoice, pocket::TrackCount> voices{};
    pocket::audio::SynthParameters parameters;
    parameters.level = 0.5F;
    for (std::size_t track = 0; track < voices.size(); ++track) {
        expect(synthVoices.configure(track, &voices[track], parameters),
               "prepare synth voice for each sequencer track");
    }
    pocket::audio::Mixer mixer;
    expect(sequencer.setInstrument(0, pocket::InstrumentType::Synth) &&
               sequencer.setRhythm(0, {2, 2, 0}),
           "configure a synth track in the sequencer");
    pocket::audio::SamplerMixerRenderer missingSynthRenderer(
        sequencer, samplerVoices, mixer, 100);
    expect(!missingSynthRenderer.isRealtimeSafe(),
           "realtime preflight rejects synth tracks without a backend");

    pocket::audio::SamplerMixerRenderer renderer(
        sequencer, samplerVoices, synthVoices, mixer, 100);
    pocket::audio::AudioCallbackProcessor processor(
        sequencer, visualEvents, renderer);
    expect(processor.isRealtimeSafe(),
           "mixed sampler and synth renderer passes realtime preflight");
    sequencer.start();

    std::array<float, 512> output{};
    const bool processed = processor.process(output.data(), 256, 0, 48000);
    bool audible = false;
    for (const auto sample : output) {
        audible = audible || std::abs(sample) > 1.0e-5F;
    }
    expect(processed && voices[0].isActive() && audible,
           "synth trigger is sample-scheduled, rendered, and routed through mixer");
}

void testInvalidInputs() {
    bool threw = false;
    try {
        (void)pocket::EuclideanGenerator::generate({1, 1, 0});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "generator rejects invalid configuration");

    pocket::Sequencer sequencer;
    expect(!sequencer.setRhythm(8, {16, 4, 0}), "reject out-of-range track index");
    expect(!sequencer.setMuted(8, true), "reject invalid mute track index");
    expect(!sequencer.setRhythm(0, {16, 0, 0}), "reject invalid live configuration");
}

void testProjectStoreRoundTrip() {
    using namespace pocket;
    Sequencer source;
    for (std::size_t i = 0; i < TrackCount; ++i) {
        source.setRhythm(i, ExamplePresetRhythms[i]);
    }
    source.setTempo(137.5);
    source.setMuted(2, true);
    source.setVolume(3, 64);
    source.setPan(4, -0.25F);
    source.setEnvelope(1, {0.01F, 1.5F});
    source.setFilter(5, {true, 3000.0F});
    auto synthSettings = source.track(5).synth;
    synthSettings.engineType = SynthEngineType::Plaits;
    synthSettings.model = 11;
    synthSettings.pitchHz = 330.0F;
    expect(source.setSynth(5, synthSettings), "test synth settings are valid");
    LfoConfig lfo;
    lfo.enabled = true;
    lfo.waveform = LfoWaveform::SawDown;
    lfo.rateHz = 0.75;
    lfo.destination = ModulationDestination::Fill;
    lfo.amount = -3;
    expect(source.setLfo(6, lfo, 0), "test LFO is valid");
    JitterConfig jitter;
    jitter.enabled = true;
    jitter.mode = JitterMode::Shift;
    jitter.intensity = 40;
    jitter.useCustomSeed = true;
    jitter.randomSeed = 4242;
    source.setJitter(7, jitter);
    TuringConfig turing;
    turing.enabled = true;
    turing.clock = TuringClockMode::Euclid;
    turing.rate = TuringRate::Quarter;
    turing.length = 12;
    turing.chance = 35;
    turing.range = 9;
    turing.scale = TuringScale::Dorian;
    turing.root = 5;
    turing.glide = 40;
    turing.drift = 25;
    turing.locked = true;
    turing.useCustomSeed = true;
    turing.randomSeed = 777;
    expect(source.setTuring(7, turing), "test Turing settings are valid");
    source.setReverbSend(3, 0.45F);
    source.setDelaySend(3, 0.65F);

    const auto original = project::captureProject(source);
    auto originalWithSample = original;
    originalWithSample.tracks[4].samplePath = "samples/my kick %1.wav";
    const auto text = project::serializeProject(originalWithSample);
    project::ProjectData parsed;
    std::string error;
    expect(project::parseProject(text, parsed, &error), "project text parses: " + error);
    expect(project::serializeProject(parsed) == text, "project text round-trips exactly");
    expect(parsed.tracks[4].samplePath == "samples/my kick %1.wav",
           "sample paths with spaces and percent signs round-trip");

    Sequencer restored;
    expect(project::applyProject(parsed, restored), "parsed project applies");
    expect(restored.tempo() == 137.5, "tempo restored");
    expect(restored.track(2).muted, "mute restored");
    expect(restored.track(3).volume == 64, "volume restored");
    expect(restored.track(3).reverbSend == 0.45F &&
               restored.track(3).delaySend == 0.65F,
           "effect sends restored");
    expect(restored.track(4).pan == -0.25F, "pan restored");
    expect(restored.track(1).envelope.decaySeconds == 1.5F, "envelope restored");
    expect(restored.track(5).filter.enabled && restored.track(5).filter.cutoffHz == 3000.0F,
           "filter restored");
    expect(restored.track(5).synth.engineType == SynthEngineType::Plaits &&
               restored.track(5).synth.model == 11 &&
               restored.track(5).synth.pitchHz == 330.0F,
           "synth engine type, model, and parameters restored");
    expect(restored.track(6).lfos[0].waveform == LfoWaveform::SawDown &&
               restored.track(6).lfos[0].amount == -3 &&
               restored.track(6).lfos[0].destination == ModulationDestination::Fill,
           "LFO restored");
    expect(restored.track(7).jitter.randomSeed == 4242 &&
               restored.track(7).jitter.mode == JitterMode::Shift,
           "jitter restored");
    const auto& restoredTuring = restored.track(7).turing;
    expect(restoredTuring.enabled &&
               restoredTuring.clock == TuringClockMode::Euclid &&
               restoredTuring.rate == TuringRate::Quarter &&
               restoredTuring.length == 12 && restoredTuring.chance == 35 &&
               restoredTuring.range == 9 &&
               restoredTuring.scale == TuringScale::Dorian &&
               restoredTuring.root == 5 && restoredTuring.glide == 40 &&
               restoredTuring.drift == 25 && restoredTuring.locked &&
               restoredTuring.useCustomSeed &&
               restoredTuring.randomSeed == 777,
           "Turing configuration restored");
    expect(restored.track(3).rhythm.length == ExamplePresetRhythms[3].length &&
               restored.track(3).rhythm.fill == ExamplePresetRhythms[3].fill,
           "rhythm restored");

    project::ProjectData untouched;
    untouched.tempoBpm = 99.0;
    expect(!project::parseProject("garbage", untouched), "reject wrong header");
    expect(!project::parseProject("PocketEuclideanProject\nversion 999\n", untouched),
           "reject unknown version");
    expect(!project::parseProject("PocketEuclideanProject\nversion 6\nbogus 1\n", untouched),
           "reject unknown key");
    expect(!project::parseProject("PocketEuclideanProject\nversion 6\ntrack.9.rhythm 1 1 0\n",
                                  untouched),
           "reject out-of-range track index");
    expect(!project::parseProject("PocketEuclideanProject\nversion 6\ntrack.0.jitter 1 9 0 0 1 0 1\n",
                                  untouched),
           "reject out-of-range enum");
    expect(untouched.tempoBpm == 99.0, "failed parse leaves output untouched");

    project::ProjectData legacy;
    expect(project::parseProject(
               "PocketEuclideanProject\n"
               "version 6\n"
               "tempo 120\n"
               "track.0.synth 110 0.5 0.4 0.3\n",
               legacy, &error),
           "version 6 project without an engine type loads: " + error);
    expect(legacy.tracks[0].synth.engineType == SynthEngineType::BasicVa &&
               legacy.tracks[0].synth.model == 7 &&
               legacy.tracks[0].synth.pitchHz == 110.0F &&
               legacy.tracks[0].synth.morph == 0.5F &&
               legacy.tracks[0].synth.timbre == 0.4F &&
               legacy.tracks[0].synth.color == 0.3F,
           "legacy synth line migrates to BasicVA with morph-derived model");
    expect(legacy.tracks[0].reverbSend == 0.0F &&
               legacy.tracks[0].delaySend == 0.0F,
           "version 6 projects load with zero effect sends");
    expect(!legacy.tracks[0].turing.enabled &&
               legacy.tracks[0].turing.length == 8,
           "version 6 projects load with the Turing machine disabled");

    project::ProjectData v10Legacy;
    expect(project::parseProject(
               "PocketEuclideanProject\n"
               "version 10\n"
               "tempo 120\n"
               "track.0.turing 1 2 1 12 40 9 3 5 15 10 1 4242\n",
               v10Legacy, &error),
           "version 10 projects load without a lock field: " + error);
    expect(v10Legacy.tracks[0].turing.enabled &&
               v10Legacy.tracks[0].turing.length == 12 &&
               v10Legacy.tracks[0].turing.randomSeed == 4242 &&
               !v10Legacy.tracks[0].turing.locked,
           "version 10 Turing lines default to unlocked");

    project::ProjectData v8Legacy;
    expect(project::parseProject(
               "PocketEuclideanProject\n"
               "version 8\n"
               "tempo 120\n"
               "track.0.synth 1 220 0.2 0.4 0.3\n"
               "track.1.sample %E3%82%AD%E3%83%83%E3%82%AF.wav\n",
               v8Legacy, &error),
           "version 8 projects still load: " + error);
    expect(v8Legacy.tracks[0].synth.engineType == SynthEngineType::Plaits &&
               v8Legacy.tracks[0].synth.model == 3,
           "version 8 Plaits morph migrates to the matching model");
    expect(v8Legacy.tracks[0].samplePath.empty(),
           "version 8 projects start with built-in samples");
    expect(v8Legacy.tracks[1].samplePath == "\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF.wav",
           "percent-encoded sample paths decode to UTF-8 bytes");

    expect(!project::parseProject(
               "PocketEuclideanProject\n"
               "version 9\n"
               "track.0.synth 1 16 110 0.5 0.4 0.3\n",
               untouched),
           "version 9 rejects an out-of-range model index");
    expect(!project::parseProject(
               "PocketEuclideanProject\n"
               "version 9\n"
               "track.0.sample bad%GGpath.wav\n",
               untouched),
           "version 9 rejects malformed path escapes");
    expect(!project::parseProject(
               "PocketEuclideanProject\n"
               "version 7\n"
               "track.0.synth 2 110 0.5 0.4 0.3\n",
               untouched),
           "version 7 rejects an out-of-range engine type");

    project::ProjectData invalid = original;
    invalid.tracks[0].rhythm = {16, 0, 0};
    Sequencer scratch;
    expect(!project::applyProject(invalid, scratch), "apply reports rejected values");

    const auto path = (std::filesystem::temp_directory_path() / "pocket_test_project" /
                       "project.pec").string();
    expect(project::saveProjectFile(path, originalWithSample, &error), "save file: " + error);
    expect(project::saveProjectFile(path, originalWithSample, &error), "overwrite file: " + error);
    project::ProjectData loaded;
    expect(project::loadProjectFile(path, loaded, &error), "load file: " + error);
    expect(project::serializeProject(loaded) == text, "file round-trips");
    expect(!std::filesystem::exists(path + ".tmp"), "no temporary file left behind");
    expect(!project::loadProjectFile(path + ".missing", loaded), "missing file fails");
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(path).parent_path(), ec);
}

void testMidiClockAndTransport() {
    using namespace pocket;
    using MidiType = platform::MidiEvent::Type;

    platform::MidiEvent event;
    MidiClockFollower clock;
    expect(clock.tempoBpm() == 120.0 && !clock.running() &&
               !clock.clockSeen(),
           "MIDI clock follower starts stopped at the default tempo");

    event.type = MidiType::Start;
    clock.process(event);
    expect(clock.takeStart() && clock.running() &&
               !clock.takeStop() && !clock.takeResume(),
           "MIDI Start requests transport start");

    // 120 BPM = 24 clocks per 500 ms = 20.833 ms per clock.
    std::uint64_t timestamp = 1000;
    for (int pulse = 0; pulse < 30; ++pulse) {
        timestamp += 20833;
        event.type = MidiType::Clock;
        event.timestampMicros = timestamp;
        clock.process(event);
    }
    expect(clock.clockSeen() &&
               std::abs(clock.tempoBpm() - 120.0) < 1.0,
           "MIDI clock estimates 120 BPM from 24 PPQN");
    expect(clock.takeTempoChanged(), "tempo update is latched for the caller");

    for (int pulse = 0; pulse < 30; ++pulse) {
        timestamp += 20000;
        event.timestampMicros = timestamp;
        clock.process(event);
    }
    expect(std::abs(clock.tempoBpm() - 125.0) < 1.0,
           "MIDI clock follows a tempo change");

    event.type = MidiType::Stop;
    clock.process(event);
    expect(clock.takeStop() && !clock.running(),
           "MIDI Stop requests transport stop");

    event.type = MidiType::Continue;
    clock.process(event);
    expect(clock.takeResume() && clock.running(),
           "MIDI Continue requests transport resume");

    const auto stableTempo = clock.tempoBpm();
    timestamp += 500000;
    event.type = MidiType::Clock;
    event.timestampMicros = timestamp;
    clock.process(event);
    expect(clock.tempoIntervalCount() == 0 &&
               clock.tempoBpm() == stableTempo,
           "long clock gaps reset the tempo window instead of bending it");

    event.type = MidiType::Start;
    clock.process(event);
    event.type = MidiType::Stop;
    clock.process(event);
    expect(clock.takeStop() && !clock.takeStart(),
           "only the latest transport message stays pending");

    Sequencer sequencer;
    sequencer.start();
    (void)sequencer.tick();
    const auto tickBeforeStop = sequencer.masterTickPosition();
    sequencer.stop();
    sequencer.resume();
    expect(sequencer.isPlaying() &&
               sequencer.masterTickPosition() == tickBeforeStop,
           "MIDI Continue resumes without resetting the tick position");
    sequencer.restart();
    expect(sequencer.isPlaying() && sequencer.masterTickPosition() == 0,
           "MIDI Start restarts from the first tick");

    Sequencer uiSide;
    Sequencer audioSide;
    audio::SequencerCommandQueue queue;
    audio::SequencerSync sync(queue, uiSide);
    uiSide.start();
    sync.sync(uiSide);
    audio::SequencerCommand command;
    expect(queue.pop(command) &&
               command.type == audio::SequencerCommandType::Start,
           "transport sync sends Start");
    audio::applySequencerCommand(audioSide, command);
    (void)audioSide.tick();
    const auto audioTickBeforeStop = audioSide.masterTickPosition();
    uiSide.stop();
    sync.sync(uiSide);
    expect(queue.pop(command) &&
               command.type == audio::SequencerCommandType::Stop,
           "transport sync sends Stop");
    audio::applySequencerCommand(audioSide, command);
    uiSide.resume();
    sync.sync(uiSide);
    expect(queue.pop(command) &&
               command.type == audio::SequencerCommandType::Continue,
           "transport sync sends Continue after a resume");
    audio::applySequencerCommand(audioSide, command);
    expect(audioSide.isPlaying() &&
               audioSide.masterTickPosition() == audioTickBeforeStop,
           "audio-side Continue preserves the transport position");
}

void testMidiClockGenerator() {
    using namespace pocket;
    MidiClockGenerator generator;
    std::uint64_t pulses = 0;
    const auto countPulse = [](void* data, std::uint32_t) noexcept {
        ++*static_cast<std::uint64_t*>(data);
    };

    generator.process(48000, 48000, 120.0, countPulse, &pulses);
    expect(pulses == 48,
           "120 BPM emits 48 clock pulses per second (24 PPQN)");

    pulses = 0;
    generator.reset();
    generator.process(24000, 48000, 60.0, countPulse, &pulses);
    generator.process(24000, 48000, 60.0, countPulse, &pulses);
    expect(pulses == 24,
           "60 BPM emits 24 clock pulses per second across block boundaries");

    pulses = 0;
    generator.reset();
    generator.process(48000, 48000, 240.0, countPulse, &pulses);
    expect(pulses == 96, "240 BPM emits 96 clock pulses per second");
}

void testMidiNoteMapping() {
    using namespace pocket;
    expect(midiNoteFromHz(440.0F) == 69,
           "440 Hz maps to MIDI note 69 (A4)");
    expect(midiNoteFromHz(110.0F) == 45,
           "110 Hz maps to MIDI note 45 (A2)");
    expect(midiNoteFromHz(110.0F, 12) == 57,
           "a +12 semitone Turing offset transposes the MIDI note");
    expect(midiNoteFromHz(20.0F, -100) == 0 &&
               midiNoteFromHz(20000.0F, 100) == 127,
           "MIDI note numbers clamp to 0..127");
    expect(midiVelocityFromVolume(100) == 127 &&
               midiVelocityFromVolume(50) == 64 &&
               midiVelocityFromVolume(0) == 1,
           "track volume maps to a usable MIDI velocity");
}

class RecordingMidiBackend final : public pocket::platform::IMidiBackend {
public:
    bool open() noexcept override { return true; }
    void close() noexcept override {}
    bool poll(pocket::platform::MidiEvent&) noexcept override { return false; }
    bool send(const pocket::platform::MidiEvent& event) noexcept override {
        events.push_back(event);
        return true;
    }
    bool hasOutput() const noexcept override { return true; }

    std::vector<pocket::platform::MidiEvent> events;
};

void testMidiOutputFromCallbackProcessor() {
    using namespace pocket;
    using MidiType = platform::MidiEvent::Type;

    {
        Sequencer sequencer;
        ui::StepTriggerEventQueue visualEvents;
        audio::AudioCallbackProcessor processor(sequencer, visualEvents);
        RecordingMidiBackend midi;
        processor.setMidiBackend(&midi);
        processor.setMidiBaseChannel(1);
        processor.setMidiOutputEnabled(true);
        std::array<float, 512> buffer{};
        // First block enables the output and establishes the transport
        // baseline without emitting a spurious Start.
        expect(processor.process(buffer.data(), 256, 0, 48000),
               "MIDI processor renders an idle block");
        sequencer.start();
        expect(processor.process(buffer.data(), 256, 256, 48000),
               "MIDI processor renders a started block");
        bool sawStart = false;
        for (const auto& event : midi.events) {
            sawStart = sawStart || event.type == MidiType::Start;
        }
        expect(sawStart, "transport start is sent as MIDI Start");
        sequencer.stop();
        expect(processor.process(buffer.data(), 256, 512, 48000),
               "MIDI processor renders a stopped block");
        bool sawStop = false;
        for (const auto& event : midi.events) {
            sawStop = sawStop || event.type == MidiType::Stop;
        }
        expect(sawStop, "transport stop is sent as MIDI Stop");
    }

    Sequencer sequencer;
    sequencer.setInstrument(0, InstrumentType::Midi);
    sequencer.setRhythm(0, {16, 16, 0});
    sequencer.setTempo(120.0);
    sequencer.start();
    ui::StepTriggerEventQueue visualEvents;
    audio::AudioCallbackProcessor processor(sequencer, visualEvents);
    RecordingMidiBackend midi;
    processor.setMidiBackend(&midi);
    processor.setMidiBaseChannel(1);
    processor.setMidiOutputEnabled(true);

    std::array<float, 512> buffer{};
    std::uint64_t blockStart = 0;
    for (int block = 0; block < 187; ++block) {
        expect(processor.process(buffer.data(), 256, blockStart, 48000),
               "MIDI note block renders");
        blockStart += 256;
    }
    expect(processor.process(buffer.data(), 128, blockStart, 48000),
           "MIDI note tail block renders");

    std::size_t clocks = 0;
    std::size_t noteOns = 0;
    std::size_t noteOffs = 0;
    std::uint8_t firstNote = 0;
    for (const auto& event : midi.events) {
        if (event.type == MidiType::Clock) {
            ++clocks;
        } else if (event.type == MidiType::NoteOn) {
            if (noteOns == 0) {
                firstNote = event.data1;
            }
            ++noteOns;
        } else if (event.type == MidiType::NoteOff) {
            ++noteOffs;
        }
    }
    expect(clocks == 48,
           "MIDI clock output is continuous for one second at 120 BPM");
    expect(noteOns == 8 && noteOffs == 8,
           "a 16/16 MIDI track sends one note per 16th step");
    expect(firstNote == 45,
           "MIDI note uses the track pitch and Turing offset");
}

void testSettingsStore() {
    using namespace pocket;
    platform::SystemSettings settings;
    settings.audio.sampleRate = 44100;
    settings.audio.bufferFrames = 512;
    settings.audio.masterVolumePercent = 65;
    settings.display.theme = platform::ThemeId::VgaDos;
    settings.input.repeatDelayMs = 400;
    settings.midi.enabled = true;
    settings.midi.inputChannel = 3;
    settings.midi.outputChannel = 5;
    settings.midi.deviceIndex = 2;
    const auto text = platform::serializeSettings(settings);
    platform::SystemSettings parsed;
    std::string error;
    expect(platform::parseSettings(text, parsed, &error), "settings parse: " + error);
    expect(platform::serializeSettings(parsed) == text, "settings round-trip");
    expect(parsed.audio.masterVolumePercent == 65 &&
               parsed.display.theme == platform::ThemeId::VgaDos &&
               parsed.midi.enabled && parsed.midi.inputChannel == 3 &&
               parsed.midi.outputChannel == 5 &&
               parsed.midi.deviceIndex == 2,
           "settings values restored");

    platform::SystemSettings untouched;
    untouched.audio.masterVolumePercent = 11;
    expect(!platform::parseSettings("junk", untouched), "settings reject header");
    expect(!platform::parseSettings("PocketEuclideanSettings\nversion 3\ntheme 9\n", untouched),
           "settings reject theme out of range");
    expect(!platform::parseSettings("PocketEuclideanSettings\nversion 3\naudio 10 512 50\n",
                                    untouched),
           "settings reject sample rate out of range");
    expect(!platform::parseSettings("PocketEuclideanSettings\nversion 3\nwat 1\n", untouched),
           "settings reject unknown key");
    expect(untouched.audio.masterVolumePercent == 11, "failed settings parse is inert");

    const auto path = (std::filesystem::temp_directory_path() /
                       "pocket_test_settings" / "settings.cfg").string();
    expect(platform::saveSettingsFile(path, settings, &error), "save settings: " + error);
    platform::SystemSettings loaded;
    expect(platform::loadSettingsFile(path, loaded, &error), "load settings: " + error);
    expect(platform::serializeSettings(loaded) == text, "settings file round-trips");
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(path).parent_path(), ec);
}

void testProjectSnapshotCoordinator() {
    std::cout << "testProjectSnapshotCoordinator...\n";
    pocket::Sequencer sequencer;
    std::array<pocket::ui::TrackAudioSettings, pocket::TrackCount> shadow{};
    for (std::size_t i = 0; i < pocket::TrackCount; ++i) {
        shadow[i].volume = static_cast<std::uint8_t>(30 + i * 8);
        shadow[i].pan = -0.7F + static_cast<float>(i) * 0.2F;
        shadow[i].reverbSend = 0.05F + static_cast<float>(i) * 0.05F;
        shadow[i].delaySend = 0.10F + static_cast<float>(i) * 0.04F;
        shadow[i].muted = (i % 2 == 1);
        shadow[i].envelope.attackSeconds = 0.05F + static_cast<float>(i) * 0.01F;
        shadow[i].envelope.decaySeconds = 0.5F + static_cast<float>(i) * 0.1F;
        shadow[i].filter.enabled = (i % 3 == 0);
        shadow[i].filter.cutoffHz = 1000.0F + static_cast<float>(i) * 500.0F;
    }
    pocket::project::ProjectEffectState fx;
    fx.delayMilliseconds = 350;
    fx.delayFeedback = 0.4F;
    fx.delayMix = 0.3F;

    const auto project = pocket::project::captureProject(sequencer, shadow, &fx);
    expect(project.effects.delayMilliseconds == 350, "fx delay time captured");
    expect(project.effects.delayFeedback == 0.4F, "fx delay feedback captured");
    expect(project.effects.delayMix == 0.3F, "fx delay mix captured");

    for (std::size_t i = 0; i < pocket::TrackCount; ++i) {
        const auto& t = project.tracks[i];
        expect(t.volume == shadow[i].volume, "track volume matches UI shadow");
        expect(t.pan == shadow[i].pan, "track pan matches UI shadow");
        expect(t.reverbSend == shadow[i].reverbSend,
               "track reverb send matches UI shadow");
        expect(t.delaySend == shadow[i].delaySend,
               "track delay send matches UI shadow");
        expect(t.muted == shadow[i].muted, "track muted matches UI shadow");
        expect(t.envelope.attackSeconds == shadow[i].envelope.attackSeconds, "track attack matches shadow");
        expect(t.envelope.decaySeconds == shadow[i].envelope.decaySeconds, "track decay matches shadow");
        expect(t.filter.enabled == shadow[i].filter.enabled, "track filter enabled matches shadow");
        expect(t.filter.cutoffHz == shadow[i].filter.cutoffHz, "track filter cutoff matches shadow");
    }

    const auto serialized = pocket::project::serializeProject(project);
    pocket::project::ProjectData parsed;
    std::string err;
    expect(pocket::project::parseProject(serialized, parsed, &err), "parsed serialized snapshot");
    expect(parsed.effects.delayMilliseconds == 350, "parsed fx delay time");
    expect(parsed.tracks[0].volume == shadow[0].volume, "parsed shadow track 0 volume");
    expect(parsed.tracks[0].reverbSend == shadow[0].reverbSend &&
               parsed.tracks[7].delaySend == shadow[7].delaySend,
           "parsed v8 project preserves effect sends");
}

void testSettingsStoreMigration() {
    std::cout << "testSettingsStoreMigration...\n";
    const std::string v2Text =
        "PocketEuclideanSettings\n"
        "version 2\n"
        "tempo 135.5\n"
        "audio 44100 256 80\n"
        "input 1 250 40\n";
    pocket::platform::SystemSettings s;
    std::string err;
    expect(pocket::platform::parseSettings(v2Text, s, &err), "parse v2 settings: " + err);
    expect(s.formatVersion == pocket::platform::SystemSettingsFormatVersion, "v2 migrated to current format version");
    expect(s.clock.initialTempoBpm == 135.5, "v2 tempo preserved");
    expect(s.audio.sampleRate == 44100, "v2 sample rate preserved");
    expect(s.audio.bufferFrames == 256, "v2 buffer frames preserved");
    expect(s.audio.masterVolumePercent == 80, "v2 master volume preserved");
    expect(s.input.preferGameController, "v2 input prefer preserved");
    expect(s.display.theme == pocket::platform::ThemeId::QuietDark, "v2 defaults theme to QuietDark");
    expect(!s.midi.enabled && s.midi.inputChannel == 1 &&
               s.midi.outputChannel == 1 && s.midi.deviceIndex == 0,
           "v2 defaults MIDI to disabled on channel 1");

    pocket::platform::SystemSettings bad;
    expect(!pocket::platform::parseSettings("PocketEuclideanSettings\nversion 0\n", bad), "reject version 0");
    expect(!pocket::platform::parseSettings("PocketEuclideanSettings\nversion 5\n", bad), "reject future version 5");

    const std::string v3Text =
        "PocketEuclideanSettings\n"
        "version 3\n"
        "tempo 120\n"
        "audio 48000 256 100\n"
        "input 1 350 120\n"
        "theme 3\n";
    expect(pocket::platform::parseSettings(v3Text, s, &err),
           "parse v3 settings without MIDI: " + err);
    expect(!s.midi.enabled && s.midi.deviceIndex == 0 &&
               s.display.theme == pocket::platform::ThemeId::NeonRgb,
           "v3 settings migrate with MIDI disabled");
}

void testDelayEffect() {
    std::cout << "testDelayEffect...\n";
    pocket::audio::DelayEffect delay;
    expect(delay.isRealtimeSafe(), "delay is realtime safe");

    pocket::project::ProjectEffectState fx;
    fx.delayMilliseconds = 100;
    fx.delayFeedback = 0.5F;
    fx.delayMix = 0.5F;
    delay.setParameters(fx);
    expect(delay.delayMilliseconds() == 100, "delayMs set");
    expect(delay.feedback() == 0.5F, "feedback set");
    expect(delay.mix() == 0.5F, "mix set");

    fx.delayMilliseconds = 5;
    fx.delayFeedback = 1.5F;
    fx.delayMix = 2.0F;
    delay.setParameters(fx);
    expect(delay.delayMilliseconds() == 10, "min delayMs clamped");
    expect(delay.feedback() == 0.90F, "max feedback clamped");
    expect(delay.mix() == 1.0F, "max mix clamped");

    fx.delayMilliseconds = 10;
    fx.delayFeedback = 0.5F;
    fx.delayMix = 0.5F;
    delay.setParameters(fx);
    delay.reset();

    constexpr std::uint32_t Rate = 48000;
    constexpr std::size_t Frames = 1000;
    std::vector<float> stereo(Frames * 2, 0.0F);
    stereo[0] = 1.0F;
    stereo[1] = 1.0F;

    delay.processStereo(stereo.data(), Frames, Rate);

    expect(std::abs(stereo[0] - 0.5F) < 1e-4F, "dry impulse attenuated by mix");
    expect(std::abs(stereo[1] - 0.5F) < 1e-4F, "dry impulse attenuated by mix");

    expect(std::abs(stereo[480 * 2] - 0.5F) < 1e-4F, "delayed echo appears at 480 frames");
    expect(std::abs(stereo[480 * 2 + 1] - 0.5F) < 1e-4F, "delayed echo appears at 480 frames");
    expect(stereo[240 * 2] == 0.0F, "silence before echo");

    fx.delayMix = 0.0F;
    delay.setParameters(fx);
    stereo.assign(Frames * 2, 0.0F);
    stereo[0] = 0.8F;
    delay.processStereo(stereo.data(), Frames, Rate);
    expect(stereo[0] == 0.8F, "mix 0 leaves input untouched");
}

void testReverbEffect() {
    std::cout << "testReverbEffect...\n";
    using namespace pocket::audio;
    ReverbEffect reverb;
    expect(reverb.isRealtimeSafe(), "reverb is realtime safe");

    constexpr std::uint32_t Rate = 48000;
    std::vector<float> stereo(4800 * 2, 0.0F);
    stereo[0] = 1.0F;
    stereo[1] = 1.0F;
    reverb.processSendStereo(stereo.data(), 4800, Rate);

    bool hasTail = false;
    bool finite = true;
    for (std::size_t i = 1; i < stereo.size(); ++i) {
        hasTail = hasTail || std::abs(stereo[i]) > 1.0e-6F;
        finite = finite && std::isfinite(stereo[i]);
    }
    expect(hasTail && finite, "reverb produces a finite wet tail");
    expect(std::abs(stereo[0]) < 0.01F,
           "reverb send bus is processed fully wet");

    reverb.reset();
    std::vector<float> silence(512 * 2, 0.0F);
    reverb.processSendStereo(silence.data(), 512, Rate);
    bool quiet = true;
    for (const float sample : silence) {
        quiet = quiet && std::abs(sample) < 1.0e-6F;
    }
    expect(quiet, "resetting the reverb clears its state");
}

} // namespace

void testTuringMachine() {
    using namespace pocket;
    std::cout << "testTuringMachine...\n";

    expect(TuringScaleMap::semitones(TuringScale::Major, 0) == 0 &&
               TuringScaleMap::semitones(TuringScale::Major, 1) == 2 &&
               TuringScaleMap::semitones(TuringScale::Major, 7) == 12 &&
               TuringScaleMap::semitones(TuringScale::Major, -1) == -1,
           "major scale degrees map across octave boundaries");
    expect(TuringScaleMap::semitones(TuringScale::Chromatic, -5) == -5 &&
               TuringScaleMap::semitones(TuringScale::MinorPentatonic, 5) ==
                   12,
           "chromatic and pentatonic scales map negative degrees");

    TuringConfig config;
    config.enabled = true;
    config.clock = TuringClockMode::Tempo;
    config.rate = TuringRate::Sixteenth;
    config.length = 8;
    config.chance = 0;
    config.range = 12;
    config.scale = TuringScale::Chromatic;
    config.root = 0;
    config.useCustomSeed = true;
    config.randomSeed = 12345;
    expect(TuringEngine::isValid(config), "default Turing config is valid");
    auto invalid = config;
    invalid.length = 1;
    expect(!TuringEngine::isValid(invalid), "reject a one-step Turing loop");
    invalid = config;
    invalid.chance = 101;
    expect(!TuringEngine::isValid(invalid), "reject chance above 100 percent");
    invalid = config;
    invalid.range = 0;
    expect(!TuringEngine::isValid(invalid), "reject a zero Turing range");
    expect(TuringEngine::ticksPerStep(TuringRate::Quarter) == 4 &&
               TuringEngine::euclidTriggersPerStep(TuringRate::Eighth) == 2 &&
               TuringEngine::freeRateHz(TuringRate::Whole) == 8.0,
           "Turing clock rate tables follow the selected sync mode");

    TuringEngine first;
    TuringEngine second;
    std::array<std::int8_t, 8> loopA{};
    std::array<std::int8_t, 8> loopB{};
    for (std::size_t tick = 0; tick < loopA.size(); ++tick) {
        const auto a = first.process(0, config, true, tick, 0.125);
        const auto b = second.process(0, config, true, tick, 0.125);
        expect(a.has_value() && b.has_value(),
               "Turing engine produces a pitch for every clock");
        loopA[tick] = a.value_or(0);
        loopB[tick] = b.value_or(0);
    }
    expect(loopA == loopB, "identical seeds produce identical phrases");
    for (std::size_t tick = 0; tick < loopA.size(); ++tick) {
        const auto next =
            first.process(0, config, true, tick + loopA.size(), 0.125);
        expect(next.has_value() && *next == loopA[tick],
               "zero chance locks the register into an exact loop");
    }

    TuringEngine evolving;
    const auto beforeEvolution = evolving.display(0, config).values;
    config.chance = 100;
    for (std::size_t tick = 0; tick < 8; ++tick) {
        (void)evolving.process(0, config, true, tick, 0.125);
    }
    expect(evolving.display(0, config).values != beforeEvolution,
           "100 percent chance rewrites the register every step");

    TuringConfig locked = config;
    locked.locked = true;
    TuringEngine lockedEngine;
    // Seed the runtime up front; the first process() would otherwise lazily
    // build the register and make the "before" snapshot meaningless.
    lockedEngine.resetTrack(0, locked);
    const auto beforeLock = lockedEngine.display(0, locked).values;
    for (std::size_t tick = 0; tick < 16; ++tick) {
        (void)lockedEngine.process(0, locked, true, tick, 0.125);
    }
    expect(lockedEngine.display(0, locked).values == beforeLock,
           "lock freezes the register even at 100 percent chance");

    TuringConfig drifty = config;
    drifty.chance = 0;
    drifty.length = 4;
    drifty.drift = 100;
    drifty.locked = false;
    TuringEngine driftEngine;
    std::array<std::int8_t, 4> firstLoop{};
    std::array<std::int8_t, 4> secondLoop{};
    for (std::size_t tick = 0; tick < firstLoop.size(); ++tick) {
        firstLoop[tick] =
            driftEngine.process(0, drifty, true, tick, 0.125).value_or(0);
    }
    for (std::size_t tick = 0; tick < secondLoop.size(); ++tick) {
        secondLoop[tick] = driftEngine
                               .process(0, drifty, true, tick + firstLoop.size(),
                                        0.125)
                               .value_or(0);
    }
    expect(firstLoop != secondLoop,
           "unlocked drift moves the phrase at the loop boundary");
    drifty.locked = true;
    TuringEngine lockedDriftEngine;
    for (std::size_t tick = 0; tick < firstLoop.size(); ++tick) {
        (void)lockedDriftEngine.process(0, drifty, true, tick, 0.125);
    }
    bool driftStayed = true;
    for (std::size_t tick = 0; tick < secondLoop.size(); ++tick) {
        driftStayed =
            driftStayed &&
            lockedDriftEngine
                    .process(0, drifty, true, tick + firstLoop.size(), 0.125)
                    .value_or(0) == firstLoop[tick];
    }
    expect(driftStayed, "lock also freezes the drift transposition");

    TuringConfig euclid = config;
    euclid.chance = 0;
    euclid.clock = TuringClockMode::Euclid;
    euclid.rate = TuringRate::Sixteenth;
    TuringEngine euclidEngine;
    for (std::size_t tick = 0; tick < 4; ++tick) {
        (void)euclidEngine.process(0, euclid, false, tick, 0.125);
    }
    expect(euclidEngine.display(0, euclid).head == 0,
           "Euclidean clock ignores ticks without a track trigger");
    (void)euclidEngine.process(0, euclid, true, 4, 0.125);
    expect(euclidEngine.display(0, euclid).head == 1,
           "Euclidean clock advances on a track trigger");

    TuringConfig freeConfig = config;
    freeConfig.clock = TuringClockMode::Free;
    freeConfig.rate = TuringRate::Whole;
    TuringEngine freeEngine;
    (void)freeEngine.process(0, freeConfig, false, 0, 0.1);
    expect(freeEngine.display(0, freeConfig).head == 0,
           "free clock waits for its own period");
    (void)freeEngine.process(0, freeConfig, false, 1, 0.1);
    expect(freeEngine.display(0, freeConfig).head == 1,
           "free clock advances independently of the transport tick");

    Sequencer sequencer;
    expect(sequencer.setTempo(120.0), "configure Turing sequencer tempo");
    TuringConfig trackConfig = config;
    trackConfig.clock = TuringClockMode::Tempo;
    trackConfig.chance = 0;
    trackConfig.length = 4;
    expect(sequencer.setTuring(0, trackConfig),
           "assign a Turing configuration to a track");
    expect(!sequencer.setTuring(TrackCount, trackConfig),
           "reject Turing assignment outside the track range");
    const auto beforeReseed = sequencer.turingDisplay(0).values;
    expect(sequencer.reseedTuring(0) &&
               sequencer.track(0).turingReseedRevision == 1 &&
               sequencer.turingDisplay(0).values != beforeReseed,
           "reseed rewrites the register and bumps the audio revision");
    sequencer.start();
    std::array<std::int8_t, 4> phrase{};
    for (std::size_t tick = 0; tick < phrase.size(); ++tick) {
        phrase[tick] = sequencer.tick().pitchOffsets[0];
    }
    for (std::size_t tick = 0; tick < phrase.size(); ++tick) {
        const auto result = sequencer.tick();
        expect(result.valid && result.pitchOffsets[0] == phrase[tick],
               "sequencer repeats the locked Turing phrase");
    }
    auto silent = trackConfig;
    silent.enabled = false;
    expect(sequencer.setTuring(0, silent), "disable the Turing generator");
    expect(sequencer.tick().pitchOffsets[0] == 0,
           "disabled Turing leaves the track at its base pitch");

    // A fresh transport must already carry the phrase pitch even when the
    // first tick is a rest: EUCLID and FREE clocks do not advance immediately.
    TuringConfig restFirstTuring = config;
    restFirstTuring.clock = TuringClockMode::Euclid;
    restFirstTuring.rate = TuringRate::Sixteenth;
    restFirstTuring.length = 4;
    restFirstTuring.chance = 0;
    restFirstTuring.scale = TuringScale::Chromatic;
    restFirstTuring.range = 12;
    restFirstTuring.useCustomSeed = true;
    restFirstTuring.randomSeed = 777;
    const EuclideanConfig restFirstRhythm{4, 1, 1};
    expect(!EuclideanGenerator::isActive(restFirstRhythm, 0),
           "rest-first rhythm really starts on a rest");
    expect(sequencer.setRhythm(0, restFirstRhythm) &&
               sequencer.setTuring(0, restFirstTuring),
           "configure a rest-first Euclidean Turing track");
    sequencer.reset();
    const auto expectedFirstPitch = sequencer.turingDisplay(0).semitone;
    const auto firstTick = sequencer.tick();
    expect(firstTick.valid && firstTick.pitchOffsets[0] == expectedFirstPitch,
           "reset preloads the Turing head for the first note");
}

int main() {
    testSettingsStore();
    testSettingsStoreMigration();
    testProjectSnapshotCoordinator();
    testDelayEffect();
    testReverbEffect();
    testProjectStoreRoundTrip();
    testPulseCountAndRotation();
    testEvenDistribution();
    testOffsetModuloAndBoundaries();
    testIndependentTracksAndTransport();
    testExamplePresetCyclesIndependently();
    testLfoModulationAndDeterminism();
    testSequencerModulationPreservesMasterPhase();
    testJitterModesAndDeterminism();
    testSequencerJitterTransportAndProjectState();
    testTuringMachine();
    testTrackMixerSettingsAndMeters();
    testAudioMixerProcessingAndMeterBridge();
    testSamplerVoicePool();
    testSynthVoiceRack();
    testBasicVaVoice();
    testSelectableSynthVoice();
    testSynthEngineParameterRouting();
    testOnePoleLowPass();
    testAudioParameterQueue();
    testSequencerCommandSync();
    testWavDecoder();
    testSampleCache();
    testVersionedSettingsAndProjectSeparation();
    testMidiClockAndTransport();
    testMidiClockGenerator();
    testMidiNoteMapping();
    testMidiOutputFromCallbackProcessor();
    testLiveChangesMuteAndTempo();
    testSampleTimeScheduling();
    testAudioCallbackProcessor();
    testSamplerMixerAudioPipeline();
    testLiveAudioParameterHandoff();
    testSynthMixerAudioPipeline();
    testInvalidInputs();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All sequencer tests passed\n";
    return EXIT_SUCCESS;
}
