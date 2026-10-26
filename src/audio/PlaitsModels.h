#pragma once

#include <cstddef>

namespace pocket::audio {

// Order matches PlaitsVoice's internal EngineType enum. The names are short
// because the SOUND screen renders them in a narrow value column with the
// built-in 5x7 bitmap font.
inline constexpr std::size_t PlaitsModelCount = 16;

inline constexpr const char* PlaitsModelNames[PlaitsModelCount] = {
    "VA",      "WTBL",    "FM",      "ADD",   "CHORD", "SPEECH",
    "BASSDR",  "SNARE",   "HIHAT",   "GRAIN", "STRING", "MODAL",
    "NOISE",   "PARTCL",  "PHSDIST", "WAVESHP"};

inline const char* plaitsModelName(std::size_t model) noexcept {
    return model < PlaitsModelCount ? PlaitsModelNames[model] : "?";
}

} // namespace pocket::audio
