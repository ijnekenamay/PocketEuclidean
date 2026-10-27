#pragma once

#include <cstddef>

namespace pocket::audio {

// Order matches PlaitsVoice's internal EngineType enum. Names are at most four
// characters so the SOUND screen can show "<number> <name>" (seven characters)
// in the value column without shrinking the font.
inline constexpr std::size_t PlaitsModelCount = 16;

inline constexpr const char* PlaitsModelNames[PlaitsModelCount] = {
    "VA",   "WTBL", "FM",   "ADD",  "CHRD", "SPCH", "BDRM", "SNRE",
    "HHAT", "GRAN", "STRN", "MODL", "NOIS", "PRTC", "PHSD", "WAVE"};

inline const char* plaitsModelName(std::size_t model) noexcept {
    return model < PlaitsModelCount ? PlaitsModelNames[model] : "?";
}

} // namespace pocket::audio
