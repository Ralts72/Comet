#include "input/input_actions.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace Comet {
    namespace {
        using namespace std::literals;
        constexpr std::pair<std::string_view, Input::Key> key_names[]{{"Space", Input::Key::Space},
            {"Escape", Input::Key::Escape}, {"Enter", Input::Key::Enter}, {"Tab", Input::Key::Tab},
            {"Backspace", Input::Key::Backspace}, {"Delete", Input::Key::Delete},
            {"Insert", Input::Key::Insert}, {"Home", Input::Key::Home}, {"End", Input::Key::End},
            {"PageUp", Input::Key::PageUp}, {"PageDown", Input::Key::PageDown},
            {"Up", Input::Key::Up}, {"Down", Input::Key::Down}, {"Left", Input::Key::Left},
            {"Right", Input::Key::Right}, {"LeftShift", Input::Key::LeftShift},
            {"RightShift", Input::Key::RightShift}, {"LeftControl", Input::Key::LeftControl},
            {"RightControl", Input::Key::RightControl}, {"LeftAlt", Input::Key::LeftAlt},
            {"RightAlt", Input::Key::RightAlt}, {"LeftSuper", Input::Key::LeftSuper},
            {"RightSuper", Input::Key::RightSuper}};
        constexpr auto mouse_button_names = std::array{"Left"sv, "Right"sv, "Middle"sv, "Extra1"sv,
            "Extra2"sv, "Extra3"sv, "Extra4"sv, "Extra5"sv};
        constexpr auto gamepad_button_names = std::array{"South"sv, "East"sv, "West"sv, "North"sv,
            "LeftShoulder"sv, "RightShoulder"sv, "Back"sv, "Start"sv, "Guide"sv, "LeftThumb"sv,
            "RightThumb"sv, "DpadUp"sv, "DpadRight"sv, "DpadDown"sv, "DpadLeft"sv};
        constexpr auto gamepad_axis_names = std::array{
            "LeftX"sv, "LeftY"sv, "RightX"sv, "RightY"sv, "LeftTrigger"sv, "RightTrigger"sv};
        constexpr auto motion_names =
            std::array{"CursorX"sv, "CursorY"sv, "ScrollX"sv, "ScrollY"sv};

        template<typename T, size_t N>
        bool named_control(std::string_view name, const std::array<std::string_view, N>& names,
            InputActions::Binding& binding) {
            const auto it = std::ranges::find(names, name);
            if(it == names.end())
                return false;
            binding.control = static_cast<T>(it - names.begin());
            return true;
        }

        template<typename T, size_t N>
        Result<InputActions::ControlName> named_control_label(
            T control, std::string_view source, const std::array<std::string_view, N>& names) {
            const auto index = static_cast<std::size_t>(control);
            if(index >= names.size())
                return Result<InputActions::ControlName>::failure(
                    "Input control cannot be serialized");
            return Result<InputActions::ControlName>::success({source, std::string(names[index])});
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
            for(const auto& [label, key] : key_names)
                if(name == label) {
                    binding.control = key;
                    return true;
                }
            return false;
        }

    }

    bool InputActions::valid_name(std::string_view name) {
        return !name.empty() && name.size() <= 64
               && name.find_first_not_of(
                      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.")
                      == std::string_view::npos;
    }

    Result<InputActions::Binding> InputActions::parse_binding(
        std::string_view source, std::string_view control, float scale, float deadzone) {
        Binding binding{Input::Key::Unknown, scale, deadzone};
        bool valid = false;
        if(source == "key")
            valid = key_control(control, binding);
        else if(source == "mouse_button")
            valid = named_control<Input::MouseButton>(control, mouse_button_names, binding);
        else if(source == "gamepad_button")
            valid = named_control<Input::GamepadButton>(control, gamepad_button_names, binding);
        else if(source == "gamepad_axis")
            valid = named_control<Input::GamepadAxis>(control, gamepad_axis_names, binding);
        else if(source == "motion")
            valid = named_control<Motion>(control, motion_names, binding);
        if(!valid)
            return Result<Binding>::failure(
                "Unknown input control: " + std::string(source) + "/" + std::string(control));
        return Result<Binding>::success(binding);
    }

    Result<InputActions::ControlName> InputActions::format_binding(const Binding& binding) {
        using Name = Result<ControlName>;
        return std::visit(
            [](const auto control) -> Name {
                using T = std::remove_cv_t<decltype(control)>;
                if constexpr(std::is_same_v<T, Input::Key>) {
                    if(control >= T::A && control <= T::Z)
                        return Name::success(
                            {"key", std::string(1, char('A' + int(control) - int(T::A)))});
                    if(control >= T::Digit0 && control <= T::Digit9)
                        return Name::success(
                            {"key", std::string(1, char('0' + int(control) - int(T::Digit0)))});
                    if(control >= T::F1 && control <= T::F25)
                        return Name::success(
                            {"key", "F" + std::to_string(int(control) - int(T::F1) + 1)});
                    for(const auto& [label, key] : key_names)
                        if(control == key)
                            return Name::success({"key", std::string(label)});
                } else {
                    if constexpr(std::is_same_v<T, Input::MouseButton>)
                        return named_control_label(control, "mouse_button", mouse_button_names);
                    else if constexpr(std::is_same_v<T, Input::GamepadButton>)
                        return named_control_label(control, "gamepad_button", gamepad_button_names);
                    else if constexpr(std::is_same_v<T, Input::GamepadAxis>)
                        return named_control_label(control, "gamepad_axis", gamepad_axis_names);
                    else
                        return named_control_label(control, "motion", motion_names);
                }
                return Name::failure("Input control cannot be serialized");
            },
            binding.control);
    }

    Result<InputActions> InputActions::create(
        std::vector<Action> actions, std::vector<Context> contexts) {
        if(actions.size() > MAX_ACTIONS)
            return Result<InputActions>::failure(
                "At most " + std::to_string(MAX_ACTIONS) + " input actions are supported");
        if(contexts.size() > MAX_CONTEXTS)
            return Result<InputActions>::failure(
                "At most " + std::to_string(MAX_CONTEXTS) + " input contexts are supported");
        std::set<std::string> context_names;
        for(const auto& context : contexts)
            if(!valid_name(context.name) || !context_names.insert(context.name).second)
                return Result<InputActions>::failure(
                    "Invalid or duplicate input context: " + context.name);
        std::set<std::string> names;
        for(const auto& action : actions) {
            if(!valid_name(action.name) || !names.insert(action.name).second)
                return Result<InputActions>::failure(
                    "Invalid or duplicate input action: " + action.name);
            if(!action.context.empty() && !context_names.contains(action.context))
                return Result<InputActions>::failure(
                    "Unknown input context for action " + action.name + ": " + action.context);
            if(action.type != Type::Button && action.type != Type::Axis
                && action.type != Type::Delta)
                return Result<InputActions>::failure("Invalid input action type: " + action.name);
            if(action.bindings.size() > MAX_BINDINGS)
                return Result<InputActions>::failure("At most " + std::to_string(MAX_BINDINGS)
                                                     + " bindings per action are supported");
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
        result.m_contexts = std::move(contexts);
        return Result<InputActions>::success(std::move(result));
    }

    void InputActions::evaluate(const Input::Frame& input, InputState& previous) const {
        Samples samples;
        sample(input, resolve_routes(m_contexts), samples);
        evaluate_samples(input, samples, previous);
    }

    InputActions::Routing InputActions::resolve_routes(std::span<const Context> contexts) const {
        std::vector<const Context*> groups;
        groups.reserve(m_actions.size());
        std::map<decltype(Binding::control), int> consumers;
        for(const auto& action : m_actions) {
            const auto found = std::ranges::find(contexts, action.context, &Context::name);
            const auto* group = found == contexts.end() ? nullptr : &*found;
            groups.push_back(group);
            if(!group || !group->enabled || !group->consume)
                continue;
            for(const auto& binding : action.bindings) {
                const auto [entry, inserted] =
                    consumers.try_emplace(binding.control, group->priority);
                if(!inserted)
                    entry->second = std::max(entry->second, group->priority);
            }
        }
        Routing routes(m_actions.size());
        for(std::size_t index = 0; index < m_actions.size(); ++index) {
            const auto* group = groups[index];
            if(group && !group->enabled)
                continue;
            const auto& bindings = m_actions[index].bindings;
            for(std::size_t binding = 0; binding < bindings.size(); ++binding) {
                const auto consumer = consumers.find(bindings[binding].control);
                routes[index].set(binding,
                    !group || consumer == consumers.end() || consumer->second <= group->priority);
            }
        }
        return routes;
    }

    InputActions::Sample InputActions::sample_binding(
        const Binding& binding, const Input::Frame& input, const std::size_t gamepad) {
        if(!input.focused)
            return {};
        return std::visit(
            [&](auto control) -> Sample {
                using T = decltype(control);
                if constexpr(std::is_same_v<T, Input::Key>) {
                    const auto state = input.key(control);
                    return {double(state.down), state, true};
                } else if constexpr(std::is_same_v<T, Input::MouseButton>) {
                    if(!input.pointer_enabled)
                        return {};
                    const auto state = input.mouse(control);
                    return {double(state.down), state, true};
                } else if constexpr(std::is_same_v<T, Input::GamepadButton>) {
                    if(gamepad < input.gamepads.size()) {
                        const auto state = input.gamepads[gamepad].button(control);
                        return {double(state.down), state, true, gamepad};
                    }
                } else if constexpr(std::is_same_v<T, Input::GamepadAxis>) {
                    if(gamepad < input.gamepads.size())
                        return {input.gamepads[gamepad].axis(control), {}, true, gamepad};
                } else {
                    if(!input.pointer_enabled)
                        return {};
                    switch(control) {
                        case Motion::CursorX:
                            return {input.cursor_delta.x, {}, true};
                        case Motion::CursorY:
                            return {input.cursor_delta.y, {}, true};
                        case Motion::ScrollX:
                            return {input.scroll.x, {}, true};
                        case Motion::ScrollY:
                            return {input.scroll.y, {}, true};
                    }
                }
                return {};
            },
            binding.control);
    }

    void InputActions::sample(
        const Input::Frame& input, const Routing& routes, Samples& samples) const {
        std::size_t gamepad = Input::MAX_GAMEPADS;
        for(std::size_t index = 0; index < input.gamepads.size(); ++index)
            if(input.gamepads[index].connected) {
                gamepad = index;
                break;
            }
        samples.resize(m_actions.size());
        for(std::size_t index = 0; index < m_actions.size(); ++index) {
            const auto& bindings = m_actions[index].bindings;
            samples[index].resize(bindings.size());
            for(std::size_t binding = 0; binding < bindings.size(); ++binding) {
                auto& source = samples[index][binding];
                source = {};
                if(routes[index].test(binding))
                    source = sample_binding(bindings[binding], input, gamepad);
                if(!std::isfinite(source.value))
                    source.value = 0;
            }
        }
    }

    void InputActions::clear_transients(Samples& samples) const {
        for(std::size_t index = 0; index < samples.size(); ++index)
            for(auto& source : samples[index]) {
                source.button.pressed = source.button.released = false;
                if(m_actions[index].type == Type::Delta)
                    source.value = 0;
            }
    }

    void InputActions::evaluate_samples(
        const Input::Frame& input, const Samples& samples, InputState& previous) const {
        previous.m_physical = input;
        for(std::size_t index = 0; index < m_actions.size(); ++index) {
            const auto& action = m_actions[index];
            auto& value = previous.m_actions[action.name];
            const bool was_down = value.down;
            value = {.type = action.type};
            bool pressed = false;
            bool released = false;
            double total = 0;
            for(std::size_t binding_index = 0; binding_index < action.bindings.size();
                ++binding_index) {
                const auto& binding = action.bindings[binding_index];
                const auto& source = samples[index][binding_index];
                if(!source.available)
                    continue;
                value.down |= source.button.down;
                pressed |= source.button.pressed;
                released |= source.button.released;
                if(std::isfinite(source.value)) {
                    const double magnitude =
                        std::max(0.0, std::abs(source.value) - binding.deadzone)
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
