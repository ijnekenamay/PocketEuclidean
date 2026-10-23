#pragma once

namespace pocket::ui {

class StepLightEnvelope {
public:
    static constexpr double AttackSeconds = 0.018;
    static constexpr double PeakHoldSeconds = 0.025;
    static constexpr double DecaySeconds = 0.170;

    bool trigger(double timestampSeconds) noexcept;
    bool valueAt(double timestampSeconds, float& luminance) const noexcept;

private:
    static double smoothStep(double value) noexcept;

    double triggeredAtSeconds_ = 0.0;
    float startingLuminance_ = 0.0F;
    bool hasTriggered_ = false;
};

} // namespace pocket::ui
