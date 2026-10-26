#include "ui/InputMapping.h"

#include <limits>

namespace pocket::ui {
namespace {

std::uint64_t addMilliseconds(std::uint64_t timestamp,
                              std::uint32_t duration) noexcept {
    const auto maxTimestamp = std::numeric_limits<std::uint64_t>::max();
    return timestamp > maxTimestamp - duration
               ? maxTimestamp
               : timestamp + duration;
}

} // namespace

ControllerMappingProfile ControllerMappingProfile::defaults() noexcept {
    ControllerMappingProfile profile;
    profile.bind(ControllerControl::DpadUp, Action::NavUp);
    profile.bind(ControllerControl::DpadDown, Action::NavDown);
    profile.bind(ControllerControl::DpadLeft, Action::NavLeft);
    profile.bind(ControllerControl::DpadRight, Action::NavRight);
    profile.bind(ControllerControl::A, Action::Confirm);
    profile.bind(ControllerControl::B, Action::Back);
    profile.bind(ControllerControl::X, Action::MuteToggle);
    profile.bind(ControllerControl::Y, Action::Help);
    profile.bind(ControllerControl::L1, Action::TrackPrevious);
    profile.bind(ControllerControl::R1, Action::TrackNext);
    profile.bind(ControllerControl::L2, Action::ScreenPrevious);
    profile.bind(ControllerControl::R2, Action::ScreenNext);
    profile.bind(ControllerControl::Start, Action::PlayStop);
    profile.bind(ControllerControl::Select, Action::ProjectMenu);
    return profile;
}

bool ControllerMappingProfile::bind(
    ControllerControl control, std::optional<Action> action) noexcept {
    const auto index = static_cast<std::size_t>(control);
    if (index >= bindings_.size() ||
        (action && static_cast<std::uint8_t>(*action) >
                       static_cast<std::uint8_t>(Action::Help))) {
        return false;
    }
    bindings_[index] = action;
    return true;
}

std::optional<Action> ControllerMappingProfile::actionFor(
    ControllerControl control) const noexcept {
    const auto index = static_cast<std::size_t>(control);
    if (index >= bindings_.size()) {
        return std::nullopt;
    }
    return bindings_[index];
}

bool ControllerMappingProfile::isValid() const noexcept {
    for (const auto& action : bindings_) {
        if (action && static_cast<std::uint8_t>(*action) >
                          static_cast<std::uint8_t>(Action::Help)) {
            return false;
        }
    }
    return true;
}

std::optional<ActionEvent> InputMapping::keyboard(
    KeyboardKey key, bool shiftHeld, bool isRepeat, bool isPressed) noexcept {
    ActionEvent event{Action::Back};
    switch (key) {
    case KeyboardKey::Up:
        event = {Action::NavUp, shiftHeld};
        break;
    case KeyboardKey::Down:
        event = {Action::NavDown, shiftHeld};
        break;
    case KeyboardKey::Left:
        event = {Action::NavLeft, shiftHeld};
        break;
    case KeyboardKey::Right:
        event = {Action::NavRight, shiftHeld};
        break;
    case KeyboardKey::Enter:
        event.action = Action::Confirm;
        break;
    case KeyboardKey::Escape:
        event.action = Action::Back;
        break;
    case KeyboardKey::Space:
        event.action = Action::PlayStop;
        break;
    case KeyboardKey::M:
        event.action = Action::MuteToggle;
        break;
    case KeyboardKey::Q:
        event.action = Action::TrackPrevious;
        break;
    case KeyboardKey::E:
        event.action = Action::TrackNext;
        break;
    case KeyboardKey::PageUp:
        event.action = Action::ScreenPrevious;
        break;
    case KeyboardKey::PageDown:
        event.action = Action::ScreenNext;
        break;
    case KeyboardKey::Tab:
        event.action = shiftHeld ? Action::ScreenPrevious : Action::ScreenNext;
        break;
    case KeyboardKey::P:
        event.action = Action::ProjectMenu;
        break;
    case KeyboardKey::F1:
        event.action = Action::Help;
        break;
    case KeyboardKey::Shift:
        event.action = Action::ValueFine;
        event.fine = isPressed;
        break;
    }

    if (isRepeat && !isRepeatable(event.action)) {
        return std::nullopt;
    }
    return event;
}

std::optional<ActionEvent> InputMapping::controller(
    ControllerControl control) noexcept {
    const auto profile = ControllerMappingProfile::defaults();
    return controller(control, profile);
}

std::optional<ActionEvent> InputMapping::controller(
    ControllerControl control,
    const ControllerMappingProfile& profile) noexcept {
    if (!profile.isValid()) {
        return std::nullopt;
    }
    const auto action = profile.actionFor(control);
    if (!action) {
        return std::nullopt;
    }
    return ActionEvent{*action};
}

bool InputMapping::isRepeatable(Action action) noexcept {
    return action == Action::NavUp || action == Action::NavDown ||
           action == Action::NavLeft || action == Action::NavRight;
}

bool InputMapping::shouldDispatch(Action action,
                                  bool pressed,
                                  bool isRepeat) noexcept {
    if (isRepeat && !isRepeatable(action)) {
        return false;
    }
    if (action == Action::ValueFine) {
        return true;
    }
    return pressed;
}

InputRepeater::InputRepeater(std::uint32_t initialDelayMs,
                             std::uint32_t normalIntervalMs,
                             std::uint32_t fastIntervalMs,
                             std::uint32_t accelerateAfterMs) noexcept
    : initialDelayMs_(initialDelayMs),
      normalIntervalMs_(normalIntervalMs == 0 ? 1 : normalIntervalMs),
      fastIntervalMs_(fastIntervalMs == 0 ? 1 : fastIntervalMs),
      accelerateAfterMs_(accelerateAfterMs) {}

std::optional<ActionEvent> InputRepeater::press(InputSource source,
                                                std::uint8_t control,
                                                ActionEvent event,
                                                std::uint64_t nowMs) noexcept {
    if (!InputMapping::isRepeatable(event.action)) {
        return event;
    }

    HeldAction* freeSlot = nullptr;
    for (auto& held : held_) {
        if (held.active && held.source == source && held.control == control) {
            return std::nullopt;
        }
        if (!held.active && freeSlot == nullptr) {
            freeSlot = &held;
        }
    }
    if (freeSlot == nullptr) {
        return event;
    }

    *freeSlot = {true, source, control, event, nowMs,
                 addMilliseconds(nowMs, initialDelayMs_)};
    return event;
}

void InputRepeater::release(InputSource source, std::uint8_t control) noexcept {
    for (auto& held : held_) {
        if (held.active && held.source == source && held.control == control) {
            held.active = false;
            return;
        }
    }
}

void InputRepeater::clear() noexcept {
    for (auto& held : held_) {
        held.active = false;
    }
    nextPollIndex_ = 0;
}

std::optional<ActionEvent> InputRepeater::poll(std::uint64_t nowMs) noexcept {
    for (std::size_t offset = 0; offset < held_.size(); ++offset) {
        const auto index = (nextPollIndex_ + offset) % held_.size();
        auto& held = held_[index];
        if (!held.active || nowMs < held.nextRepeatAtMs) {
            continue;
        }

        const auto heldDuration = nowMs - held.pressedAtMs;
        const auto interval = heldDuration >= accelerateAfterMs_
                                  ? fastIntervalMs_
                                  : normalIntervalMs_;
        held.nextRepeatAtMs = addMilliseconds(nowMs, interval);
        nextPollIndex_ = (index + 1) % held_.size();
        return held.event;
    }
    return std::nullopt;
}

} // namespace pocket::ui
