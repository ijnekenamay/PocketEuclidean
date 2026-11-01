#pragma once

#include "sequencer/Sequencer.h"

#include <cstddef>

namespace pocket::audio {

// Names are at most four characters so the SOUND screen can show
// "<number> <name>" (seven characters) in the value column without shrinking
// the font.

// Plaits engine order matches the hardware firmware (plaits::Voice).
inline constexpr std::size_t PlaitsModelCount = 24;
inline constexpr const char* PlaitsModelNames[PlaitsModelCount] = {
    "VCF",  "PD",   "FM-A", "FM-B", "FM-C", "TERR", "STRM", "CHIP",
    "VA",   "WSHP", "FM",   "GRAN", "ADD",  "WTBL", "CHRD", "SPCH",
    "SWRM", "NOIS", "PRTC", "STRG", "MODL", "BDRM", "SNRE", "HHAT"};

// Braids macro-oscillator shapes in firmware order, without the hidden "?".
inline constexpr std::size_t BraidsModelCount = 47;
inline constexpr const char* BraidsModelNames[BraidsModelCount] = {
    "CSAW", "MORP", "SAWS", "SINT", "BUZZ", "SQSB", "SWSB", "SQSY",
    "SWSY", "3SAW", "3SQR", "3TRI", "3SIN", "3RNG", "SWRM", "COMB",
    "TOY",  "LPF",  "PKF",  "BPF",  "HPF",  "VOSM", "VOWL", "VWFO",
    "HARM", "FM",   "FBFM", "CHFM", "PLUK", "BOWD", "BLWN", "FLUT",
    "BELL", "DRUM", "KICK", "CYMB", "SNAR", "WTBL", "WMAP", "WLIN",
    "WPAR", "FNOI", "TWIN", "CLKN", "CLOU", "PRTC", "DMOD"};

// Rings resonator models in firmware order.
inline constexpr std::size_t RingsModelCount = 6;
inline constexpr const char* RingsModelNames[RingsModelCount] = {
    "MODL", "SYMP", "STRG", "FM", "SYMQ", "STRV"};

inline const char* plaitsModelName(std::size_t model) noexcept {
    return model < PlaitsModelCount ? PlaitsModelNames[model] : "?";
}

inline const char* synthModelName(SynthEngineType engine,
                                  std::size_t model) noexcept {
    switch (engine) {
    case SynthEngineType::Plaits:
        return plaitsModelName(model);
    case SynthEngineType::Braids:
        return model < BraidsModelCount ? BraidsModelNames[model] : "?";
    case SynthEngineType::Rings:
        return model < RingsModelCount ? RingsModelNames[model] : "?";
    case SynthEngineType::BasicVa:
        break;
    }
    return "VA";
}

// Models saved by project formats before v12 used the original sixteen-model
// order; this maps them onto the Plaits engine order.
inline constexpr unsigned char LegacyPlaitsModelMap[16] = {
    8, 13, 10, 12, 14, 15, 21, 22, 23, 11, 19, 20, 17, 18, 1, 9};

} // namespace pocket::audio
