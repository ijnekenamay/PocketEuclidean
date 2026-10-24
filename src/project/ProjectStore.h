#pragma once

#include "project/ProjectData.h"

#include <array>
#include <iosfwd>
#include <string>

namespace pocket::ui {
struct TrackAudioSettings;
}

namespace pocket::project {

ProjectData captureProject(const Sequencer& sequencer);

// Captures project data combining the Sequencer state with UI-side audio shadows
// (volume, pan, mute, envelope, filter) and optional effect state.
ProjectData captureProject(
    const Sequencer& sequencer,
    const std::array<ui::TrackAudioSettings, TrackCount>& audioShadow,
    const ProjectEffectState* effects = nullptr);

// Applies every value through the Sequencer setters, so out-of-range data is
// rejected by the same validation as live edits. Returns false if any value
// was rejected (valid values are still applied).
bool applyProject(const ProjectData& project, Sequencer& sequencer);

std::string serializeProject(const ProjectData& project);

// Strict parser: unknown keys, malformed numbers, out-of-range indices and a
// wrong format version all fail and leave `out` untouched.
bool parseProject(const std::string& text, ProjectData& out,
                  std::string* error = nullptr);

// Writes to "<path>.tmp" and renames, so a crash or power loss mid-write
// never destroys the previous save.
bool saveProjectFile(const std::string& path, const ProjectData& project,
                     std::string* error = nullptr);
bool loadProjectFile(const std::string& path, ProjectData& out,
                     std::string* error = nullptr);

} // namespace pocket::project
