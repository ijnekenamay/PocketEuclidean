#include "sequencer/EuclideanGenerator.h"

#include <stdexcept>

namespace pocket {

bool EuclideanGenerator::isValid(const EuclideanConfig& config) noexcept {
    return config.length >= MinLength &&
           config.length <= MaxLength &&
           config.fill >= 1 &&
           config.fill <= config.length &&
           config.offset <= MaxOffset;
}

std::vector<bool> EuclideanGenerator::generate(const EuclideanConfig& config) {
    if (!isValid(config)) {
        throw std::invalid_argument("Invalid Euclidean rhythm configuration");
    }

    std::vector<bool> pattern(config.length, false);
    const auto rotation = static_cast<std::uint8_t>(config.offset % config.length);
    for (std::uint8_t step = 0; step < config.length; ++step) {
        const auto unrotated = static_cast<std::uint8_t>(
            (step + config.length - rotation) % config.length);
        pattern[step] =
            ((static_cast<unsigned int>(unrotated) * config.fill) % config.length) <
            config.fill;
    }
    return pattern;
}

bool EuclideanGenerator::isActive(const EuclideanConfig& config, std::uint8_t step) {
    if (!isValid(config)) {
        throw std::invalid_argument("Invalid Euclidean rhythm configuration");
    }
    if (step >= config.length) {
        throw std::out_of_range("Euclidean step is outside the pattern");
    }

    const auto rotation = static_cast<std::uint8_t>(config.offset % config.length);
    const auto unrotated = static_cast<std::uint8_t>(
        (step + config.length - rotation) % config.length);
    return ((static_cast<unsigned int>(unrotated) * config.fill) % config.length) <
           config.fill;
}

} // namespace pocket
