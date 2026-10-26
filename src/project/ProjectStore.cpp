#include "project/ProjectStore.h"
#include "ui/UiController.h"

#include <cstdio>
#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>
#include <filesystem>

namespace pocket::project {

namespace {

constexpr int LfoWaveformMax = static_cast<int>(LfoWaveform::SampleAndHold);
constexpr int LfoRateModeMax = static_cast<int>(LfoRateMode::Synchronized);
constexpr int LfoDivisionMax = static_cast<int>(LfoDivision::Count) - 1;
constexpr int DestinationMax = static_cast<int>(ModulationDestination::Offset);
constexpr int JitterModeMax = static_cast<int>(JitterMode::Mix);
constexpr int JitterRateMax = static_cast<int>(JitterRate::Whole);

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

template <typename T>
bool readValue(std::istringstream& in, T& value) {
    return static_cast<bool>(in >> value);
}

// Reads small integers through int so they are not parsed as characters.
template <typename T>
bool readInt(std::istringstream& in, T& value, int minimum, int maximum) {
    int raw = 0;
    if (!(in >> raw) || raw < minimum || raw > maximum) {
        return false;
    }
    value = static_cast<T>(raw);
    return true;
}

bool readFloat(std::istringstream& in, float& value) {
    float raw = 0.0F;
    if (!(in >> raw) || !std::isfinite(raw)) {
        return false;
    }
    value = raw;
    return true;
}

bool readDouble(std::istringstream& in, double& value) {
    double raw = 0.0;
    if (!(in >> raw) || !std::isfinite(raw)) {
        return false;
    }
    value = raw;
    return true;
}

bool parseTrackLine(const std::string& key, std::istringstream& in,
                    ProjectData& project, std::uint32_t version) {
    // key is "track.<n>.<field>[.<m>]"
    const auto first = key.find('.');
    const auto second = key.find('.', first + 1);
    if (second == std::string::npos) {
        return false;
    }
    const auto indexText = key.substr(first + 1, second - first - 1);
    if (indexText.size() != 1 || indexText[0] < '0' ||
        indexText[0] >= static_cast<char>('0' + TrackCount)) {
        return false;
    }
    auto& track = project.tracks[static_cast<std::size_t>(indexText[0] - '0')];
    const auto field = key.substr(second + 1);

    if (field == "rhythm") {
        return readInt(in, track.rhythm.length, 0, 255) &&
               readInt(in, track.rhythm.fill, 0, 255) &&
               readInt(in, track.rhythm.offset, 0, 255);
    }
    if (field.rfind("lfo.", 0) == 0) {
        const auto lfoText = field.substr(4);
        if (lfoText.size() != 1 || lfoText[0] < '0' ||
            lfoText[0] >= static_cast<char>('0' + MaxLfosPerTrack)) {
            return false;
        }
        auto& lfo = track.lfos[static_cast<std::size_t>(lfoText[0] - '0')];
        int enabled = 0;
        return readInt(in, enabled, 0, 1) && ((lfo.enabled = enabled != 0), true) &&
               readInt(in, lfo.waveform, 0, LfoWaveformMax) &&
               readInt(in, lfo.rateMode, 0, LfoRateModeMax) &&
               readDouble(in, lfo.rateHz) &&
               readInt(in, lfo.division, 0, LfoDivisionMax) &&
               readInt(in, lfo.destination, 0, DestinationMax) &&
               readInt(in, lfo.amount, -128, 127);
    }
    if (field == "jitter") {
        int enabled = 0;
        int useSeed = 0;
        auto& jitter = track.jitter;
        if (!(readInt(in, enabled, 0, 1) &&
              readInt(in, jitter.mode, 0, JitterModeMax) &&
              readInt(in, jitter.intensity, 0, 255) &&
              readInt(in, jitter.rate, 0, JitterRateMax) &&
              readInt(in, jitter.shiftRange, 0, 255) &&
              readInt(in, useSeed, 0, 1) && readValue(in, jitter.randomSeed))) {
            return false;
        }
        jitter.enabled = enabled != 0;
        jitter.useCustomSeed = useSeed != 0;
        return true;
    }
    if (field == "mix") {
        int muted = 0;
        if (!(readInt(in, muted, 0, 1) &&
              readInt(in, track.instrument, 0,
                      static_cast<int>(InstrumentType::Synth)) &&
              readInt(in, track.volume, 0, 255) && readFloat(in, track.pan))) {
            return false;
        }
        track.muted = muted != 0;
        if (version >= 8) {
            // Format v8 appended the reverb and delay send levels.
            if (!readFloat(in, track.reverbSend) ||
                !readFloat(in, track.delaySend) ||
                track.reverbSend < 0.0F || track.reverbSend > 1.0F ||
                track.delaySend < 0.0F || track.delaySend > 1.0F) {
                return false;
            }
        }
        return true;
    }
    if (field == "env") {
        return readFloat(in, track.envelope.attackSeconds) &&
               readFloat(in, track.envelope.decaySeconds);
    }
    if (field == "filter") {
        int enabled = 0;
        if (!(readInt(in, enabled, 0, 1) &&
              readFloat(in, track.filter.cutoffHz))) {
            return false;
        }
        track.filter.enabled = enabled != 0;
        return true;
    }
    if (field == "synth") {
        // Format v7 added the per-track engine type as the first field.
        // Older files stored only the four continuous parameters.
        if (version >= 7) {
            int engineType = 0;
            if (!readInt(in, engineType, 0, 1)) {
                return false;
            }
            track.synth.engineType = static_cast<SynthEngineType>(engineType);
        }
        return readFloat(in, track.synth.pitchHz) &&
               readFloat(in, track.synth.morph) &&
               readFloat(in, track.synth.timbre) &&
               readFloat(in, track.synth.color);
    }
    return false;
}

} // namespace

ProjectData captureProject(const Sequencer& sequencer) {
    ProjectData project;
    project.tempoBpm = sequencer.tempo();
    for (std::size_t i = 0; i < TrackCount; ++i) {
        const auto& source = sequencer.track(i);
        auto& target = project.tracks[i];
        target.rhythm = source.rhythm;
        target.lfos = source.lfos;
        target.jitter = source.jitter;
        target.muted = source.muted;
        target.instrument = source.instrument;
        target.volume = source.volume;
        target.pan = source.pan;
        target.reverbSend = source.reverbSend;
        target.delaySend = source.delaySend;
        target.envelope = source.envelope;
        target.filter = source.filter;
        target.synth = source.synth;
    }
    return project;
}

ProjectData captureProject(
    const Sequencer& sequencer,
    const std::array<ui::TrackAudioSettings, TrackCount>& audioShadow,
    const ProjectEffectState* effects) {
    ProjectData project = captureProject(sequencer);
    for (std::size_t i = 0; i < TrackCount; ++i) {
        const auto& shadow = audioShadow[i];
        auto& target = project.tracks[i];
        target.volume = shadow.volume;
        target.pan = shadow.pan;
        target.reverbSend = shadow.reverbSend;
        target.delaySend = shadow.delaySend;
        target.muted = shadow.muted;
        target.envelope = shadow.envelope;
        target.filter = shadow.filter;
        target.synth = shadow.synth;
    }
    if (effects != nullptr) {
        project.effects = *effects;
    }
    return project;
}

bool applyProject(const ProjectData& project, Sequencer& sequencer) {
    bool ok = sequencer.setTempo(project.tempoBpm);
    for (std::size_t i = 0; i < TrackCount; ++i) {
        const auto& source = project.tracks[i];
        ok = sequencer.setRhythm(i, source.rhythm) && ok;
        for (std::size_t lfo = 0; lfo < MaxLfosPerTrack; ++lfo) {
            ok = sequencer.setLfo(i, source.lfos[lfo], lfo) && ok;
        }
        ok = sequencer.setJitter(i, source.jitter) && ok;
        ok = sequencer.setMuted(i, source.muted) && ok;
        ok = sequencer.setInstrument(i, source.instrument) && ok;
        ok = sequencer.setVolume(i, source.volume) && ok;
        ok = sequencer.setPan(i, source.pan) && ok;
        ok = sequencer.setReverbSend(i, source.reverbSend) && ok;
        ok = sequencer.setDelaySend(i, source.delaySend) && ok;
        ok = sequencer.setEnvelope(i, source.envelope) && ok;
        ok = sequencer.setFilter(i, source.filter) && ok;
        ok = sequencer.setSynth(i, source.synth) && ok;
    }
    return ok;
}

std::string serializeProject(const ProjectData& project) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(9);
    out << "PocketEuclideanProject\n";
    out << "version " << project.formatVersion << '\n';
    out << "tempo " << project.tempoBpm << '\n';
    for (std::size_t i = 0; i < TrackCount; ++i) {
        const auto& t = project.tracks[i];
        const auto prefix = "track." + std::to_string(i) + ".";
        out << prefix << "rhythm " << int(t.rhythm.length) << ' '
            << int(t.rhythm.fill) << ' ' << int(t.rhythm.offset) << '\n';
        for (std::size_t m = 0; m < MaxLfosPerTrack; ++m) {
            const auto& l = t.lfos[m];
            out << prefix << "lfo." << m << ' ' << int(l.enabled) << ' '
                << int(l.waveform) << ' ' << int(l.rateMode) << ' '
                << l.rateHz << ' ' << int(l.division) << ' '
                << int(l.destination) << ' ' << int(l.amount) << '\n';
        }
        const auto& j = t.jitter;
        out << prefix << "jitter " << int(j.enabled) << ' ' << int(j.mode)
            << ' ' << int(j.intensity) << ' ' << int(j.rate) << ' '
            << int(j.shiftRange) << ' ' << int(j.useCustomSeed) << ' '
            << j.randomSeed << '\n';
        out << prefix << "mix " << int(t.muted) << ' ' << int(t.instrument)
            << ' ' << int(t.volume) << ' ' << t.pan << ' ' << t.reverbSend
            << ' ' << t.delaySend << '\n';
        out << prefix << "env " << t.envelope.attackSeconds << ' '
            << t.envelope.decaySeconds << '\n';
        out << prefix << "filter " << int(t.filter.enabled) << ' '
            << t.filter.cutoffHz << '\n';
        out << prefix << "synth " << static_cast<int>(t.synth.engineType) << ' '
            << t.synth.pitchHz << ' ' << t.synth.morph << ' ' << t.synth.timbre << ' '
            << t.synth.color << '\n';
    }
    const auto& fx = project.effects;
    out << "fx " << fx.delayMilliseconds << ' ' << fx.delayFeedback << ' '
        << fx.delayMix << ' ' << fx.filterCutoffHz << ' '
        << int(fx.filterResponse) << '\n';
    return out.str();
}

bool parseProject(const std::string& text, ProjectData& out,
                  std::string* error) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    std::string line;
    if (!std::getline(stream, line) ||
        line.substr(0, line.find('\r')) != "PocketEuclideanProject") {
        return fail(error, "not a Pocket Euclidean project file");
    }

    ProjectData project;
    bool versionSeen = false;
    int lineNumber = 1;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        std::istringstream in(line);
        in.imbue(std::locale::classic());
        std::string key;
        in >> key;
        bool ok = false;
        if (key == "version") {
            std::uint32_t version = 0;
            ok = readValue(in, version);
            if (ok && (version < OldestSupportedProjectVersion ||
                       version > ProjectFormatVersion)) {
                return fail(error, "unsupported project version " +
                                       std::to_string(version));
            }
            if (ok) {
                project.formatVersion = version;
            }
            versionSeen = ok;
        } else if (key == "tempo") {
            ok = readDouble(in, project.tempoBpm);
        } else if (key == "fx") {
            auto& fx = project.effects;
            ok = readInt(in, fx.delayMilliseconds, 0, 65535) &&
                 readFloat(in, fx.delayFeedback) &&
                 readFloat(in, fx.delayMix) &&
                 readFloat(in, fx.filterCutoffHz) &&
                 readInt(in, fx.filterResponse, 0, 255);
        } else if (key.rfind("track.", 0) == 0) {
            if (!versionSeen) {
                return fail(error, "missing version before track data");
            }
            ok = parseTrackLine(key, in, project, project.formatVersion);
        }
        if (!ok) {
            return fail(error, "invalid line " + std::to_string(lineNumber));
        }
    }
    if (!versionSeen) {
        return fail(error, "missing version");
    }
    out = project;
    return true;
}

bool saveProjectFile(const std::string& path, const ProjectData& project,
                     std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
    }
    const auto temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << serializeProject(project);
        file.flush();
        if (!file) {
            return fail(error, "cannot write " + temporary);
        }
    }
    fs::rename(temporary, target, ec);
    if (ec) {
        // Windows cannot rename over an existing file on some filesystems.
        fs::remove(target, ec);
        ec.clear();
        fs::rename(temporary, target, ec);
        if (ec) {
            return fail(error, "cannot replace " + path + ": " + ec.message());
        }
    }
    return true;
}

bool loadProjectFile(const std::string& path, ProjectData& out,
                     std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(error, "cannot open " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseProject(buffer.str(), out, error);
}

} // namespace pocket::project
