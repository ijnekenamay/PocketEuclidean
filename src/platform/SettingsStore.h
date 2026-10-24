#pragma once

#include "platform/SystemSettings.h"

#include <string>

namespace pocket::platform {

std::string serializeSettings(const SystemSettings& settings);

// Strict: unknown keys, malformed or out-of-range values and a wrong version
// are rejected and `out` is left untouched.
bool parseSettings(const std::string& text, SystemSettings& out,
                   std::string* error = nullptr);

// Atomic (temp file + rename) like project saves.
bool saveSettingsFile(const std::string& path, const SystemSettings& settings,
                      std::string* error = nullptr);
bool loadSettingsFile(const std::string& path, SystemSettings& out,
                      std::string* error = nullptr);

} // namespace pocket::platform
