#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace pocket::ui {

enum class Action : std::uint8_t {
    NavUp,
    NavDown,
    NavLeft,
    NavRight,
    Confirm,
    Back,
    PlayStop,
    MuteToggle,
    TrackPrevious,
    TrackNext,
    ScreenPrevious,
    ScreenNext,
    ProjectMenu,
    ValueFine,
    Help
};

enum class KeyboardKey : std::uint8_t {
    Up,
    Down,
    Left,
    Right,
    Enter,
    Escape,
    Space,
    M,
    Q,
    E,
    PageUp,
    PageDown,
    Tab,
    P,
    F1,
    Shift
};

enum class ControllerControl : std::uint8_t {
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    A,
    B,
    X,
    Y,
    L1,
    R1,
    L2,
    R2,
    Start,
    Select,
    Count
};

enum class InputSource : std::uint8_t {
    Keyboard,
    Controller
};

struct ActionEvent {
    Action action;
    bool fine = false;
};

constexpr std::size_t ControllerControlCount =
    static_cast<std::size_t>(ControllerControl::Count);

class ControllerMappingProfile {
public:
    static ControllerMappingProfile defaults() noexcept;

    bool bind(ControllerControl control,
              std::optional<Action> action) noexcept;
    std::optional<Action> actionFor(ControllerControl control) const noexcept;
    bool isValid() const noexcept;

private:
    std::array<std::optional<Action>, ControllerControlCount> bindings_{};
};

class InputMapping {
public:
    static std::optional<ActionEvent> keyboard(
        KeyboardKey key,
        bool shiftHeld = false,
        bool isRepeat = false,
        bool isPressed = true) noexcept;
    static std::optional<ActionEvent> controller(
        ControllerControl control) noexcept;
    static std::optional<ActionEvent> controller(
        ControllerControl control,
        const ControllerMappingProfile& profile) noexcept;
    static bool isRepeatable(Action action) noexcept;
    // Returns true when the SDL adapter should dispatch this action for the
    // given edge. Edge-triggered actions (transport, confirm, back, mute,
    // screen changes, ...) fire on the press edge only, so releasing the key
    // or button cannot apply the action a second time. ValueFine tracks both
    // edges because it is a modifier state. OS key-repeat events never
    // dispatch; held directional input is driven by InputRepeater instead.
    static bool shouldDispatch(Action action,
                               bool pressed,
                               bool isRepeat = false) noexcept;
};

class InputRepeater {
public:
    static constexpr std::size_t MaxHeldControls = 32;

    explicit InputRepeater(std::uint32_t initialDelayMs = 350,
                           std::uint32_t normalIntervalMs = 120,
                           std::uint32_t fastIntervalMs = 55,
                           std::uint32_t accelerateAfterMs = 900) noexcept;

    std::optional<ActionEvent> press(InputSource source,
                                     std::uint8_t control,
                                     ActionEvent event,
                                     std::uint64_t nowMs) noexcept;
    void release(InputSource source, std::uint8_t control) noexcept;
    // Forgets every held control. Call when focus is lost or an input device
    // disconnects so a missed release cannot leave a direction repeating.
    void clear() noexcept;
    std::optional<ActionEvent> poll(std::uint64_t nowMs) noexcept;

private:
    struct HeldAction {
        bool active = false;
        InputSource source = InputSource::Keyboard;
        std::uint8_t control = 0;
        ActionEvent event{Action::NavUp};
        std::uint64_t pressedAtMs = 0;
        std::uint64_t nextRepeatAtMs = 0;
    };

    std::array<HeldAction, MaxHeldControls> held_{};
    std::size_t nextPollIndex_ = 0;
    std::uint32_t initialDelayMs_;
    std::uint32_t normalIntervalMs_;
    std::uint32_t fastIntervalMs_;
    std::uint32_t accelerateAfterMs_;
};

} // namespace pocket::ui
