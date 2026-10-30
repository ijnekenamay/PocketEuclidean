#pragma once

#include "platform/BackendInterfaces.h"

#include <cstdint>
#include <iosfwd>
#include <memory>

namespace pocket::app {

// True when the build has a real MIDI adapter. The SYSTEM menu hides the MIDI
// category when this is false.
bool systemMidiInputAvailable() noexcept;

// Creates the platform MIDI adapter. Unsupported platforms return a backend
// whose open() always fails, so callers can keep one code path.
std::unique_ptr<platform::IMidiBackend> makeSystemMidiBackend(
    std::uint8_t deviceIndex);

// Writes one line per available input/output port, for --midi-list.
void listSystemMidiInputs(std::ostream& out);

} // namespace pocket::app
