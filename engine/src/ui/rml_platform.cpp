#include "rml_platform.h"

#include "core/window.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Input.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Comet::Ui {
    namespace {
        using Input = Comet::Input;
        using UiEvent = Comet::Window::UiEvent;

        Rml::Input::KeyIdentifier translate_key(const Input::Key key) {
            using Key = Input::Key;
            using namespace Rml::Input;
            const auto range = [](Key value, Key first, Key last, KeyIdentifier target) {
                if(value < first || value > last)
                    return KI_UNKNOWN;
                return static_cast<KeyIdentifier>(
                    static_cast<int>(target) + static_cast<int>(value) - static_cast<int>(first));
            };
            if(const auto value = range(key, Key::A, Key::Z, KI_A); value != KI_UNKNOWN)
                return value;
            if(const auto value = range(key, Key::Digit0, Key::Digit9, KI_0); value != KI_UNKNOWN)
                return value;
            if(const auto value = range(key, Key::F1, Key::F24, KI_F1); value != KI_UNKNOWN)
                return value;
            if(const auto value = range(key, Key::Keypad0, Key::Keypad9, KI_NUMPAD0);
                value != KI_UNKNOWN)
                return value;

            constexpr std::array keys{std::pair{Key::Space, KI_SPACE},
                std::pair{Key::Apostrophe, KI_OEM_7}, std::pair{Key::Comma, KI_OEM_COMMA},
                std::pair{Key::Minus, KI_OEM_MINUS}, std::pair{Key::Period, KI_OEM_PERIOD},
                std::pair{Key::Slash, KI_OEM_2}, std::pair{Key::Semicolon, KI_OEM_1},
                std::pair{Key::Equal, KI_OEM_PLUS}, std::pair{Key::LeftBracket, KI_OEM_4},
                std::pair{Key::Backslash, KI_OEM_5}, std::pair{Key::RightBracket, KI_OEM_6},
                std::pair{Key::GraveAccent, KI_OEM_3}, std::pair{Key::Escape, KI_ESCAPE},
                std::pair{Key::Enter, KI_RETURN}, std::pair{Key::Tab, KI_TAB},
                std::pair{Key::Backspace, KI_BACK}, std::pair{Key::Insert, KI_INSERT},
                std::pair{Key::Delete, KI_DELETE}, std::pair{Key::Right, KI_RIGHT},
                std::pair{Key::Left, KI_LEFT}, std::pair{Key::Down, KI_DOWN},
                std::pair{Key::Up, KI_UP}, std::pair{Key::PageUp, KI_PRIOR},
                std::pair{Key::PageDown, KI_NEXT}, std::pair{Key::Home, KI_HOME},
                std::pair{Key::End, KI_END}, std::pair{Key::CapsLock, KI_CAPITAL},
                std::pair{Key::ScrollLock, KI_SCROLL}, std::pair{Key::NumLock, KI_NUMLOCK},
                std::pair{Key::PrintScreen, KI_SNAPSHOT}, std::pair{Key::Pause, KI_PAUSE},
                std::pair{Key::KeypadDecimal, KI_DECIMAL}, std::pair{Key::KeypadDivide, KI_DIVIDE},
                std::pair{Key::KeypadMultiply, KI_MULTIPLY},
                std::pair{Key::KeypadSubtract, KI_SUBTRACT}, std::pair{Key::KeypadAdd, KI_ADD},
                std::pair{Key::KeypadEnter, KI_NUMPADENTER},
                std::pair{Key::KeypadEqual, KI_OEM_NEC_EQUAL}, std::pair{Key::LeftShift, KI_LSHIFT},
                std::pair{Key::RightShift, KI_RSHIFT}, std::pair{Key::LeftControl, KI_LCONTROL},
                std::pair{Key::RightControl, KI_RCONTROL}, std::pair{Key::LeftAlt, KI_LMENU},
                std::pair{Key::RightAlt, KI_RMENU}, std::pair{Key::LeftSuper, KI_LMETA},
                std::pair{Key::RightSuper, KI_RMETA}, std::pair{Key::Menu, KI_APPS}};
            for(const auto& [source, target] : keys)
                if(key == source)
                    return target;
            return KI_UNKNOWN;
        }

        int translate_modifiers(const uint8_t modifiers) {
            int result = 0;
            if(modifiers & UiEvent::Shift)
                result |= Rml::Input::KM_SHIFT;
            if(modifiers & UiEvent::Control)
                result |= Rml::Input::KM_CTRL;
            if(modifiers & UiEvent::Alt)
                result |= Rml::Input::KM_ALT;
            if(modifiers & UiEvent::Super)
                result |= Rml::Input::KM_META;
            if(modifiers & UiEvent::CapsLock)
                result |= Rml::Input::KM_CAPSLOCK;
            if(modifiers & UiEvent::NumLock)
                result |= Rml::Input::KM_NUMLOCK;
            return result;
        }

        int pixel_coordinate(const float coordinate, const double scale) {
            const auto value = std::round(static_cast<double>(coordinate) * scale);
            return static_cast<int>(
                std::clamp(value, static_cast<double>(std::numeric_limits<int>::min()),
                    static_cast<double>(std::numeric_limits<int>::max())));
        }

        int navigation_direction(const Input::GamepadState& pad) {
            using Button = Input::GamepadButton;
            if(pad.button(Button::DpadUp).down)
                return 0;
            if(pad.button(Button::DpadDown).down)
                return 1;
            if(pad.button(Button::DpadLeft).down)
                return 2;
            if(pad.button(Button::DpadRight).down)
                return 3;
            const float x = pad.axis(Input::GamepadAxis::LeftX);
            const float y = pad.axis(Input::GamepadAxis::LeftY);
            if(std::max(std::abs(x), std::abs(y)) < 0.65f)
                return -1;
            if(std::abs(x) > std::abs(y))
                return x < 0 ? 2 : 3;
            return y < 0 ? 0 : 1;
        }
    }

    void RmlPlatform::refresh_text_input(Rml::Context& context) {
        m_text_input_active = false;
        if(m_capture_active)
            return;
        const auto* focus = context.GetFocusElement();
        if(!focus || focus->HasAttribute("disabled"))
            return;
        const auto& tag = focus->GetTagName();
        if(tag == "textarea") {
            m_text_input_active = true;
            return;
        }
        if(tag != "input")
            return;
        const auto type = focus->GetAttribute<Rml::String>("type", "text");
        m_text_input_active = type == "text" || type == "password";
    }

    void RmlPlatform::cancel_input(Rml::Context& context) {
        release_input(context, true);
    }

    void RmlPlatform::stop_dispatch_preserve_focus(Rml::Context& context) {
        release_input(context, false);
    }

    void RmlPlatform::release_input(Rml::Context& context, const bool blur_focus) {
        m_cancelled = true;
        m_text_input_active = false;
        if(m_cancelling)
            return;
        m_cancelling = true;
        // 先移除 hover 与 focus，再补 release，防止取消拖动时生成点击。
        context.ProcessMouseLeave();
        if(blur_focus)
            if(auto* focus = context.GetFocusElement())
                focus->Blur();
        const auto keys = std::exchange(m_keys_down, {});
        const auto buttons = std::exchange(m_mouse_down, {});
        for(size_t index = 0; index < keys.size(); ++index)
            if(keys[index])
                context.ProcessKeyUp(translate_key(static_cast<Input::Key>(index)), 0);
        for(size_t index = 0; index < buttons.size(); ++index)
            if(buttons[index])
                context.ProcessMouseButtonUp(static_cast<int>(index), 0);
        m_navigation_direction = -1;
        m_navigation_blocked = true;
        m_gamepad.reset();
        m_cancelling = false;
    }

    void RmlPlatform::update_gamepad(Rml::Context& context, const Input::Frame& frame) {
        const auto index = frame.first_connected_gamepad();
        if(!index) {
            m_gamepad.reset();
            m_navigation_direction = -1;
            return;
        }
        const auto& pad = frame.gamepads[*index];
        const int direction = navigation_direction(pad);
        if(m_gamepad != index || m_connection_revision != pad.connection_revision) {
            m_gamepad = index;
            m_connection_revision = pad.connection_revision;
            m_navigation_direction = -1;
            m_navigation_blocked = direction != -1;
        }
        if(direction == -1) {
            m_navigation_direction = -1;
            m_navigation_blocked = false;
        } else if(!m_navigation_blocked) {
            const auto now = std::chrono::steady_clock::now();
            const bool changed = direction != m_navigation_direction;
            if(changed || now >= m_next_navigation) {
                constexpr std::array keys{Rml::Input::KI_UP, Rml::Input::KI_DOWN,
                    Rml::Input::KI_LEFT, Rml::Input::KI_RIGHT};
                const auto key = keys[static_cast<size_t>(direction)];
                context.ProcessKeyDown(key, 0);
                if(m_cancelled)
                    return;
                context.ProcessKeyUp(key, 0);
                m_navigation_direction = direction;
                m_next_navigation =
                    now
                    + (changed ? std::chrono::milliseconds(350) : std::chrono::milliseconds(90));
            }
        }
        if(m_cancelled)
            return;
        if(pad.button(Input::GamepadButton::South).pressed) {
            context.ProcessKeyDown(Rml::Input::KI_RETURN, 0);
            if(!m_cancelled)
                context.ProcessKeyUp(Rml::Input::KI_RETURN, 0);
        }
        if(!m_cancelled && pad.button(Input::GamepadButton::East).pressed) {
            context.ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
            if(!m_cancelled)
                context.ProcessKeyUp(Rml::Input::KI_ESCAPE, 0);
        }
    }

    void RmlPlatform::update(Rml::Context& context, const Comet::Window& window,
        const Input::Frame& frame, const bool modal_open) {
        const auto framebuffer = window.get_framebuffer_size();
        const auto size = window.get_size();
        const auto scale = window.get_content_scale();
        context.SetDimensions({static_cast<int>(framebuffer.x), static_cast<int>(framebuffer.y)});
        context.SetDensityIndependentPixelRatio(scale.x);

        const bool fresh = !m_serial || frame.serial != *m_serial;
        const bool skipped = m_serial && frame.serial != *m_serial && frame.serial != *m_serial + 1;
        const bool interrupted = m_serial && (frame.interruption != m_interruption || skipped);
        const bool acquiring = modal_open && !m_modal_open;
        const bool closing = !modal_open && m_modal_open;
        const bool capture_changed = m_capture_active != m_previous_capture;
        const bool enabled = frame.focused && framebuffer.x && framebuffer.y && size.x && size.y;
        m_cancelled = false;
        if(interrupted || closing || (!enabled && m_window_accepting))
            cancel_input(context);
        else if(capture_changed)
            release_input(context, false);
        if(interrupted || acquiring || closing || capture_changed || !enabled) {
            m_cancelled = true;
            m_gate.read(frame, false);
            m_pointer_gate.read(frame, false);
        }
        const auto& input = m_gate.read(frame, enabled && modal_open);
        const auto& pointer =
            m_pointer_gate.read(frame, enabled && !m_capture_active && !window.is_cursor_locked());
        m_serial = frame.serial;
        m_interruption = frame.interruption;
        m_modal_open = modal_open;
        m_window_accepting = enabled;
        m_previous_capture = m_capture_active;
        if(m_cancelled && fresh && modal_open && frame.focused) {
            m_gamepad = frame.first_connected_gamepad();
            m_navigation_direction = -1;
            m_navigation_blocked = true;
            if(m_gamepad) {
                const auto& pad = frame.gamepads[*m_gamepad];
                m_connection_revision = pad.connection_revision;
                m_navigation_blocked = navigation_direction(pad) != -1;
            }
        }
        if(!pointer.pointer_enabled) {
            context.ProcessMouseLeave();
            const auto buttons = std::exchange(m_mouse_down, {});
            for(size_t index = 0; index < buttons.size(); ++index)
                if(buttons[index])
                    context.ProcessMouseButtonUp(static_cast<int>(index), 0);
        }
        if(!fresh || m_cancelled || !enabled)
            return;

        const double x_scale = static_cast<double>(framebuffer.x) / size.x;
        const double y_scale = static_cast<double>(framebuffer.y) / size.y;
        refresh_text_input(context);
        for(const auto& event : window.get_ui_events()) {
            if(m_cancelled)
                break;
            const int modifiers = translate_modifiers(event.modifiers);
            switch(event.type) {
                case UiEvent::Type::KeyDown: {
                    if(!modal_open || !input.focused || m_capture_active)
                        break;
                    const auto key = translate_key(event.key);
                    const auto index = static_cast<size_t>(event.key);
                    // Gate 重新取得所有权会清聚合边沿；有序事件仍可识别本帧的新点击。
                    if(key == Rml::Input::KI_UNKNOWN
                        || (event.repeat ? !m_keys_down[index] : !frame.key(event.key).pressed))
                        break;
                    m_keys_down.set(index);
                    context.ProcessKeyDown(key, modifiers);
                    if(!m_cancelled
                        && (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER))
                        context.ProcessTextInput('\n');
                    break;
                }
                case UiEvent::Type::KeyUp: {
                    const auto index = static_cast<size_t>(event.key);
                    if(m_keys_down[index]) {
                        m_keys_down.reset(index);
                        context.ProcessKeyUp(translate_key(event.key), modifiers);
                    }
                    break;
                }
                case UiEvent::Type::Text:
                    refresh_text_input(context);
                    if(modal_open && input.focused && m_text_input_active && !m_capture_active)
                        context.ProcessTextInput(static_cast<Rml::Character>(event.codepoint));
                    break;
                case UiEvent::Type::MouseMove:
                    if(pointer.pointer_enabled)
                        context.ProcessMouseMove(pixel_coordinate(event.position.x, x_scale),
                            pixel_coordinate(event.position.y, y_scale), modifiers);
                    break;
                case UiEvent::Type::MouseDown: {
                    if(!pointer.pointer_enabled || !frame.mouse(event.button).pressed)
                        break;
                    const auto index = static_cast<size_t>(event.button);
                    context.ProcessMouseMove(pixel_coordinate(event.position.x, x_scale),
                        pixel_coordinate(event.position.y, y_scale), modifiers);
                    if(m_cancelled)
                        break;
                    m_mouse_down.set(index);
                    context.ProcessMouseButtonDown(static_cast<int>(index), modifiers);
                    break;
                }
                case UiEvent::Type::MouseUp: {
                    const auto index = static_cast<size_t>(event.button);
                    if(m_mouse_down[index]) {
                        context.ProcessMouseMove(pixel_coordinate(event.position.x, x_scale),
                            pixel_coordinate(event.position.y, y_scale), modifiers);
                        if(m_cancelled)
                            break;
                        m_mouse_down.reset(index);
                        context.ProcessMouseButtonUp(static_cast<int>(index), modifiers);
                    }
                    break;
                }
                case UiEvent::Type::Scroll:
                    if(pointer.pointer_enabled)
                        context.ProcessMouseWheel(
                            {-event.position.x, -event.position.y}, modifiers);
                    break;
                case UiEvent::Type::Focus:
                    if(!event.focused)
                        cancel_input(context);
                    break;
                case UiEvent::Type::PointerLeave:
                    context.ProcessMouseLeave();
                    break;
            }
        }
        if(!m_cancelled && modal_open && input.focused && !m_capture_active)
            update_gamepad(context, frame);
        if(!m_cancelled && modal_open)
            refresh_text_input(context);
        else
            m_text_input_active = false;
    }
}
