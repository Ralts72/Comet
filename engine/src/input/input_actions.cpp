#include "input/input_actions.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>

namespace Comet {
    namespace {
        template<typename T, size_t N>
        bool named_control(std::string_view name, const std::array<std::string_view, N>& names,
            InputActions::Binding& binding) {
            const auto it = std::ranges::find(names, name);
            if(it == names.end())
                return false;
            binding.control = static_cast<T>(it - names.begin());
            return true;
        }

        bool key_control(std::string_view name, InputActions::Binding& binding) {
            using Key = Input::Key;
            if(name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') {
                binding.control = static_cast<Key>(int(Key::A) + name[0] - 'A');
                return true;
            }
            if(name.size() == 1 && name[0] >= '0' && name[0] <= '9') {
                binding.control = static_cast<Key>(int(Key::Digit0) + name[0] - '0');
                return true;
            }
            if(name.starts_with('F')) {
                unsigned number = 0;
                const auto [end, error] =
                    std::from_chars(name.data() + 1, name.data() + name.size(), number);
                if(error == std::errc{} && end == name.data() + name.size() && number >= 1
                    && number <= 25) {
                    binding.control = static_cast<Key>(int(Key::F1) + number - 1);
                    return true;
                }
            }
            const std::pair<std::string_view, Key> names[]{{"Space", Key::Space},
                {"Escape", Key::Escape}, {"Enter", Key::Enter}, {"Tab", Key::Tab},
                {"Backspace", Key::Backspace}, {"Delete", Key::Delete}, {"Insert", Key::Insert},
                {"Home", Key::Home}, {"End", Key::End}, {"PageUp", Key::PageUp},
                {"PageDown", Key::PageDown}, {"Up", Key::Up}, {"Down", Key::Down},
                {"Left", Key::Left}, {"Right", Key::Right}, {"LeftShift", Key::LeftShift},
                {"RightShift", Key::RightShift}, {"LeftControl", Key::LeftControl},
                {"RightControl", Key::RightControl}, {"LeftAlt", Key::LeftAlt},
                {"RightAlt", Key::RightAlt}, {"LeftSuper", Key::LeftSuper},
                {"RightSuper", Key::RightSuper}};
            for(const auto& [label, key] : names)
                if(name == label) {
                    binding.control = key;
                    return true;
                }
            return false;
        }

        struct Sample {
            float value = 0;
            Input::ButtonState button;
        };

        Sample sample(const InputActions::Binding& binding, const Input::Frame& input,
            const Input::GamepadState* pad) {
            return std::visit(
                [&](auto control) -> Sample {
                    using T = decltype(control);
                    if constexpr(std::is_same_v<T, Input::Key>) {
                        const auto state = input.key(control);
                        return {float(state.down && input.focused), state};
                    } else if constexpr(std::is_same_v<T, Input::MouseButton>) {
                        const auto state = input.mouse(control);
                        return {float(state.down && input.focused), state};
                    } else if constexpr(std::is_same_v<T, Input::GamepadButton>) {
                        if(pad)
                            return {float(pad->button(control).down), pad->button(control)};
                    } else if constexpr(std::is_same_v<T, Input::GamepadAxis>) {
                        if(pad)
                            return {pad->axis(control), {}};
                    } else if(input.focused) {
                        switch(control) {
                            case InputActions::Motion::CursorX:
                                return {input.cursor_delta.x, {}};
                            case InputActions::Motion::CursorY:
                                return {input.cursor_delta.y, {}};
                            case InputActions::Motion::ScrollX:
                                return {input.scroll.x, {}};
                            case InputActions::Motion::ScrollY:
                                return {input.scroll.y, {}};
                        }
                    }
                    return {};
                },
                binding.control);
        }
    }

    Result<InputActions::Binding> InputActions::parse_binding(
        std::string_view source, std::string_view control, float scale, float deadzone) {
        using namespace std::literals;
        Binding binding{Input::Key::Unknown, scale, deadzone};
        bool valid = false;
        if(source == "key")
            valid = key_control(control, binding);
        else if(source == "mouse_button")
            valid = named_control<Input::MouseButton>(control,
                std::array{"Left"sv, "Right"sv, "Middle"sv, "Extra1"sv, "Extra2"sv, "Extra3"sv,
                    "Extra4"sv, "Extra5"sv},
                binding);
        else if(source == "gamepad_button")
            valid = named_control<Input::GamepadButton>(control,
                std::array{"South"sv, "East"sv, "West"sv, "North"sv, "LeftShoulder"sv,
                    "RightShoulder"sv, "Back"sv, "Start"sv, "Guide"sv, "LeftThumb"sv,
                    "RightThumb"sv, "DpadUp"sv, "DpadRight"sv, "DpadDown"sv, "DpadLeft"sv},
                binding);
        else if(source == "gamepad_axis")
            valid = named_control<Input::GamepadAxis>(control,
                std::array{"LeftX"sv, "LeftY"sv, "RightX"sv, "RightY"sv, "LeftTrigger"sv,
                    "RightTrigger"sv},
                binding);
        else if(source == "motion")
            valid = named_control<Motion>(
                control, std::array{"CursorX"sv, "CursorY"sv, "ScrollX"sv, "ScrollY"sv}, binding);
        if(!valid)
            return Result<Binding>::failure(
                "Unknown input control: " + std::string(source) + "/" + std::string(control));
        return Result<Binding>::success(binding);
    }

    Result<InputActions> InputActions::create(std::vector<Action> actions) {
        if(actions.size() > 128)
            return Result<InputActions>::failure("At most 128 input actions are supported");
        std::set<std::string> names;
        for(const auto& action : actions) {
            if(action.name.empty() || action.name.size() > 64
                || action.name.find_first_not_of(
                       "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.")
                       != std::string::npos
                || !names.insert(action.name).second)
                return Result<InputActions>::failure(
                    "Invalid or duplicate input action: " + action.name);
            if(action.type != Type::Button && action.type != Type::Axis
                && action.type != Type::Delta)
                return Result<InputActions>::failure("Invalid input action type: " + action.name);
            if(action.bindings.size() > 16)
                return Result<InputActions>::failure(
                    "At most 16 bindings per action are supported");
            for(const auto& binding : action.bindings) {
                const bool valid_control = std::visit(
                    [](auto control) {
                        using T = decltype(control);
                        if constexpr(std::is_same_v<T, Motion>)
                            return control >= Motion::CursorX && control <= Motion::ScrollY;
                        else if constexpr(std::is_same_v<T, Input::Key>)
                            return control > T::Unknown && control < T::Count;
                        else
                            return control >= T(0) && control < T::Count;
                    },
                    binding.control);
                const bool motion = std::holds_alternative<Motion>(binding.control);
                const bool axis = std::holds_alternative<Input::GamepadAxis>(binding.control);
                if(!valid_control || motion != (action.type == Type::Delta)
                    || (axis && action.type == Type::Button) || !std::isfinite(binding.scale)
                    || std::abs(binding.scale) > 100 || !std::isfinite(binding.deadzone)
                    || binding.deadzone < 0 || binding.deadzone >= 1
                    || (!axis && binding.deadzone != 0)
                    || (action.type == Type::Button && binding.scale != 1))
                    return Result<InputActions>::failure(
                        "Invalid binding for action: " + action.name);
            }
        }
        InputActions result;
        result.m_actions = std::move(actions);
        return Result<InputActions>::success(std::move(result));
    }

    void InputActions::evaluate(const Input::Frame& input, InputState& previous) const {
        previous.m_physical = input;
        const Input::GamepadState* pad = nullptr;
        if(input.focused)
            for(const auto& candidate : input.gamepads)
                if(candidate.connected) {
                    pad = &candidate;
                    break;
                }
        for(const auto& action : m_actions) {
            auto& value = previous.m_actions[action.name];
            const bool was_down = value.down;
            value = {.type = action.type};
            bool pressed = false;
            bool released = false;
            double total = 0;
            for(const auto& binding : action.bindings) {
                const auto source = sample(binding, input, pad);
                value.down |= source.button.down && input.focused;
                pressed |= source.button.pressed && input.focused;
                released |= source.button.released;
                if(std::isfinite(source.value)) {
                    float magnitude = std::max(0.0f, std::abs(source.value) - binding.deadzone)
                                      / (1 - binding.deadzone);
                    total += std::copysign(magnitude, source.value) * binding.scale;
                }
            }
            if(action.type == Type::Button) {
                value.value = float(value.down);
                value.pressed = pressed && !was_down;
                value.released = !value.down && (was_down || released);
            } else {
                value.down = false;
                if(action.type == Type::Axis)
                    total = std::clamp(total, -1.0, 1.0);
                if(std::isfinite(total) && std::abs(total) <= std::numeric_limits<float>::max())
                    value.value = static_cast<float>(total);
            }
        }
    }
}
