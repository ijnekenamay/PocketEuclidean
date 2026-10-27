#include "platform/SettingsStore.h"

#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>

namespace pocket::platform {

namespace {

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

} // namespace

std::string serializeSettings(const SystemSettings& s) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "PocketEuclideanSettings\n";
    out << "version " << s.formatVersion << '\n';
    out << "tempo " << s.clock.initialTempoBpm << '\n';
    out << "audio " << s.audio.sampleRate << ' ' << s.audio.bufferFrames << ' '
        << int(s.audio.masterVolumePercent) << '\n';
    out << "input " << int(s.input.preferGameController) << ' '
        << s.input.repeatDelayMs << ' ' << s.input.repeatIntervalMs << '\n';
    out << "midi " << int(s.midi.enabled) << ' ' << int(s.midi.inputChannel)
        << ' ' << int(s.midi.outputChannel) << ' ' << int(s.midi.deviceIndex)
        << '\n';
    out << "theme " << int(s.display.theme) << '\n';
    return out.str();
}

bool parseSettings(const std::string& text, SystemSettings& out,
                   std::string* error) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    std::string line;
    if (!std::getline(stream, line) ||
        line.substr(0, line.find('\r')) != "PocketEuclideanSettings") {
        return fail(error, "not a Pocket Euclidean settings file");
    }

    SystemSettings s;
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
            ok = static_cast<bool>(in >> version);
            if (ok && (version == 0 || version > SystemSettingsFormatVersion)) {
                return fail(error, "unsupported settings version " +
                                       std::to_string(version));
            }
            versionSeen = ok;
        } else if (key == "tempo") {
            ok = static_cast<bool>(in >> s.clock.initialTempoBpm);
        } else if (key == "audio") {
            int master = 0;
            ok = static_cast<bool>(in >> s.audio.sampleRate >>
                                   s.audio.bufferFrames >> master) &&
                 master >= 0 && master <= 100;
            s.audio.masterVolumePercent = static_cast<std::uint8_t>(master);
        } else if (key == "input") {
            int prefer = 0;
            ok = static_cast<bool>(in >> prefer >> s.input.repeatDelayMs >>
                                   s.input.repeatIntervalMs) &&
                 (prefer == 0 || prefer == 1);
            s.input.preferGameController = prefer == 1;
        } else if (key == "midi") {
            int enabled = 0;
            int inputChannel = 1;
            int outputChannel = 1;
            int deviceIndex = 0;
            ok = static_cast<bool>(in >> enabled >> inputChannel >>
                                   outputChannel >> deviceIndex) &&
                 (enabled == 0 || enabled == 1) && inputChannel >= 1 &&
                 inputChannel <= 16 && outputChannel >= 1 &&
                 outputChannel <= 16 && deviceIndex >= 0 &&
                 deviceIndex <= 15;
            if (ok) {
                s.midi.enabled = enabled == 1;
                s.midi.inputChannel = static_cast<std::uint8_t>(inputChannel);
                s.midi.outputChannel = static_cast<std::uint8_t>(outputChannel);
                s.midi.deviceIndex = static_cast<std::uint8_t>(deviceIndex);
            }
        } else if (key == "theme") {
            int theme = 0;
            ok = static_cast<bool>(in >> theme) && theme >= 0 &&
                 theme <= static_cast<int>(ThemeId::VgaDos);
            s.display.theme = static_cast<ThemeId>(theme);
        }
        if (!ok) {
            return fail(error, "invalid line " + std::to_string(lineNumber));
        }
    }
    if (!versionSeen) {
        return fail(error, "missing version");
    }
    s.formatVersion = SystemSettingsFormatVersion;
    if (!isValid(s)) {
        return fail(error, "values out of range");
    }
    out = s;
    return true;
}

bool saveSettingsFile(const std::string& path, const SystemSettings& settings,
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
        file << serializeSettings(settings);
        file.flush();
        if (!file) {
            return fail(error, "cannot write " + temporary);
        }
    }
    fs::rename(temporary, target, ec);
    if (ec) {
        fs::remove(target, ec);
        ec.clear();
        fs::rename(temporary, target, ec);
        if (ec) {
            return fail(error, "cannot replace " + path + ": " + ec.message());
        }
    }
    return true;
}

bool loadSettingsFile(const std::string& path, SystemSettings& out,
                      std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(error, "cannot open " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseSettings(buffer.str(), out, error);
}

} // namespace pocket::platform
