#pragma once

#include <cstdint>
#include <vector>

namespace pocket {

struct EuclideanConfig {
    std::uint8_t length = 16;
    std::uint8_t fill = 4;
    std::uint8_t offset = 0;
};

class EuclideanGenerator {
public:
    static constexpr std::uint8_t MinLength = 2;
    static constexpr std::uint8_t MaxLength = 32;
    static constexpr std::uint8_t MaxOffset = 31;

    static bool isValid(const EuclideanConfig& config) noexcept;
    static std::vector<bool> generate(const EuclideanConfig& config);
    static bool isActive(const EuclideanConfig& config, std::uint8_t step);
};

} // namespace pocket
