#include "core/input.h"

#include <algorithm>
#include <stdexcept>

namespace Comet {
    namespace {
        void update_button(Input::ButtonState& button, bool down) {
            if(button.down == down)
                return;
            button.down = down;
            button.pressed |= down;
            button.released |= !down;
        }

        void clear_edges(auto& buttons) {
            for(auto& button : buttons)
                button.pressed = button.released = false;
        }

        void release_buttons(auto& buttons) {
            for(auto& button : buttons) {
                update_button(button, false);
                button.pressed = false;
            }
        }

        void accumulate(Math::Vec2& target, const Math::Vec2 value) {
            if(Math::is_finite(value) && Math::is_finite(target + value))
                target += value;
        }
    }

    void Input::key_event(const Key key, const bool down) {
        const auto index = static_cast<size_t>(key);
        if(m_pending.focused && key != Key::Unknown && index < m_pending.keys.size())
            update_button(m_pending.keys[index], down);
    }

    void Input::mouse_button_event(const MouseButton button, const bool down) {
        const auto index = static_cast<size_t>(button);
        if(m_pending.focused && index < m_pending.mouse_buttons.size())
            update_button(m_pending.mouse_buttons[index], down);
    }

    void Input::cursor_event(const Math::Vec2 position) {
        if(!Math::is_finite(position))
            return;
        if(m_pending.focused && m_has_cursor_position)
            accumulate(m_pending.cursor_delta, position - m_pending.cursor_position);
        m_pending.cursor_position = position;
        m_has_cursor_position = m_pending.focused;
    }

    void Input::scroll_event(const Math::Vec2 offset) {
        if(m_pending.focused)
            accumulate(m_pending.scroll, offset);
    }

    void Input::focus_event(const bool focused) {
        if(m_pending.focused == focused)
            return;
        m_pending.focused = focused;
        m_has_cursor_position = false;
        m_pending.cursor_delta = {};
        m_pending.scroll = {};
        if(focused) {
            m_gamepad_baseline.fill(true);
            return;
        }
        m_pending.release_controls();
    }

    void Input::gamepad_sample(
        const size_t index, const std::optional<GamepadSample>& sample) {
        if(index >= MAX_GAMEPADS)
            return;
        auto& gamepad = m_pending.gamepads[index];
        gamepad.connected = sample.has_value();
        const bool active = sample && m_pending.focused;
        for(size_t button = 0; button < gamepad.buttons.size(); ++button) {
            update_button(gamepad.buttons[button], active && sample->buttons[button]);
            if(m_gamepad_baseline[index])
                gamepad.buttons[button].pressed = false;
        }
        for(size_t axis = 0; axis < gamepad.axes.size(); ++axis) {
            float value = active ? sample->axes[axis] : 0;
            if(!std::isfinite(value))
                value = 0;
            const float minimum =
                axis < static_cast<size_t>(GamepadAxis::LeftTrigger) ? -1.0f : 0.0f;
            gamepad.axes[axis] = std::clamp(value, minimum, 1.0f);
        }
        m_gamepad_baseline[index] = false;
    }

    const Input::Frame& Input::publish_frame() {
        ++m_pending.serial;
        m_frame = m_pending;
        m_pending.clear_transients();
        return m_frame;
    }

    void Input::Frame::clear_transients() {
        clear_edges(keys);
        clear_edges(mouse_buttons);
        for(auto& gamepad : gamepads)
            clear_edges(gamepad.buttons);
        cursor_delta = {};
        scroll = {};
    }

    void Input::Frame::release_controls() {
        release_buttons(keys);
        release_buttons(mouse_buttons);
        for(auto& gamepad : gamepads) {
            release_buttons(gamepad.buttons);
            gamepad.axes.fill(0);
        }
        cursor_delta = {};
        scroll = {};
    }

    const Input::Frame& Input::Gate::read(const Frame& source, bool enabled) {
        if(m_source_serial && source.serial < *m_source_serial)
            throw std::invalid_argument("Input source serial moved backwards");
        const bool accepting = enabled && source.focused && !m_interrupted;
        const bool fresh = !m_source_serial || source.serial != *m_source_serial;
        if(!fresh && accepting == m_accepting && !m_interrupted)
            return m_frame;
        const bool acquiring = accepting && !m_accepting;
        Frame next = source;
        next.serial = m_frame.serial + 1;
        next.focused = accepting;
        size_t index = 0;
        const auto route = [&](auto& target, const auto& previous) {
            for(size_t i = 0; i < target.size(); ++i, ++index) {
                auto& button = target[i];
                if(!accepting) {
                    m_blocked[index] = button.down;
                    button = {.released = previous[i].down};
                    continue;
                }
                const bool blocked = m_blocked[index] || acquiring;
                m_blocked[index] = blocked && button.down;
                if(blocked) {
                    button = {};
                } else if(!fresh) {
                    button.pressed = false;
                    button.released = false;
                }
            }
        };
        route(next.keys, m_frame.keys);
        route(next.mouse_buttons, m_frame.mouse_buttons);
        for(size_t pad = 0; pad < next.gamepads.size(); ++pad) {
            route(next.gamepads[pad].buttons, m_frame.gamepads[pad].buttons);
            if(!accepting || acquiring)
                next.gamepads[pad].axes.fill(0);
        }
        if(!accepting || acquiring || !fresh) {
            next.cursor_delta = {};
            next.scroll = {};
        }
        m_frame = next;
        m_source_serial = source.serial;
        m_accepting = accepting;
        m_interrupted = false;
        return m_frame;
    }
}
