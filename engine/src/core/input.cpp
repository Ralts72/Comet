#include "core/input.h"

#include <algorithm>

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
        release_buttons(m_pending.keys);
        release_buttons(m_pending.mouse_buttons);
        for(auto& gamepad : m_pending.gamepads) {
            release_buttons(gamepad.buttons);
            gamepad.axes.fill(0);
        }
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
        clear_edges(m_pending.keys);
        clear_edges(m_pending.mouse_buttons);
        for(auto& gamepad : m_pending.gamepads)
            clear_edges(gamepad.buttons);
        m_pending.cursor_delta = {};
        m_pending.scroll = {};
        return m_frame;
    }
}
