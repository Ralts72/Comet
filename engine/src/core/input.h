#pragma once

#include "common/export.h"
#include "core/math_utils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Comet {
    // 事件先累积、再一次性发布；不依赖窗口、ImGui 或游戏操作映射。
    class COMET_API Input {
    public:
        enum class Key {
            Unknown,
            A,
            B,
            C,
            D,
            E,
            F,
            G,
            H,
            I,
            J,
            K,
            L,
            M,
            N,
            O,
            P,
            Q,
            R,
            S,
            T,
            U,
            V,
            W,
            X,
            Y,
            Z,
            Digit0,
            Digit1,
            Digit2,
            Digit3,
            Digit4,
            Digit5,
            Digit6,
            Digit7,
            Digit8,
            Digit9,
            F1,
            F2,
            F3,
            F4,
            F5,
            F6,
            F7,
            F8,
            F9,
            F10,
            F11,
            F12,
            F13,
            F14,
            F15,
            F16,
            F17,
            F18,
            F19,
            F20,
            F21,
            F22,
            F23,
            F24,
            F25,
            Keypad0,
            Keypad1,
            Keypad2,
            Keypad3,
            Keypad4,
            Keypad5,
            Keypad6,
            Keypad7,
            Keypad8,
            Keypad9,
            Space,
            Apostrophe,
            Comma,
            Minus,
            Period,
            Slash,
            Semicolon,
            Equal,
            LeftBracket,
            Backslash,
            RightBracket,
            GraveAccent,
            World1,
            World2,
            Escape,
            Enter,
            Tab,
            Backspace,
            Insert,
            Delete,
            Right,
            Left,
            Down,
            Up,
            PageUp,
            PageDown,
            Home,
            End,
            CapsLock,
            ScrollLock,
            NumLock,
            PrintScreen,
            Pause,
            KeypadDecimal,
            KeypadDivide,
            KeypadMultiply,
            KeypadSubtract,
            KeypadAdd,
            KeypadEnter,
            KeypadEqual,
            LeftShift,
            LeftControl,
            LeftAlt,
            LeftSuper,
            RightShift,
            RightControl,
            RightAlt,
            RightSuper,
            Menu,
            Count
        };
        enum class MouseButton {
            Left,
            Right,
            Middle,
            Extra1,
            Extra2,
            Extra3,
            Extra4,
            Extra5,
            Count
        };
        enum class GamepadButton {
            South,
            East,
            West,
            North,
            LeftShoulder,
            RightShoulder,
            Back,
            Start,
            Guide,
            LeftThumb,
            RightThumb,
            DpadUp,
            DpadRight,
            DpadDown,
            DpadLeft,
            Count
        };
        enum class GamepadAxis {
            LeftX,
            LeftY,
            RightX,
            RightY,
            LeftTrigger,
            RightTrigger,
            Count
        };
        static constexpr size_t MAX_GAMEPADS = 16;

        struct ButtonState {
            bool down = false;
            bool pressed = false;
            bool released = false;
        };
        struct GamepadSample {
            std::array<bool, static_cast<size_t>(GamepadButton::Count)> buttons{};
            // 摇杆 [-1,1]，Y 向下；扳机 [0,1]。死区属于上层操作映射。
            std::array<float, static_cast<size_t>(GamepadAxis::Count)> axes{};
        };
        struct GamepadState {
            bool connected = false;
            std::array<ButtonState, static_cast<size_t>(GamepadButton::Count)> buttons{};
            std::array<float, static_cast<size_t>(GamepadAxis::Count)> axes{};
            [[nodiscard]] const ButtonState& button(GamepadButton value) const {
                return buttons.at(static_cast<size_t>(value));
            }
            [[nodiscard]] float axis(GamepadAxis value) const {
                return axes.at(static_cast<size_t>(value));
            }
        };
        struct Frame {
            uint64_t serial = 0;
            bool focused = false;
            std::array<ButtonState, static_cast<size_t>(Key::Count)> keys{};
            std::array<ButtonState, static_cast<size_t>(MouseButton::Count)>
                mouse_buttons{};
            Math::Vec2 cursor_position{};
            Math::Vec2 cursor_delta{};
            Math::Vec2 scroll{};
            std::array<GamepadState, MAX_GAMEPADS> gamepads{};

            [[nodiscard]] const ButtonState& key(Key value) const {
                return keys.at(static_cast<size_t>(value));
            }
            [[nodiscard]] const ButtonState& mouse(MouseButton value) const {
                return mouse_buttons.at(static_cast<size_t>(value));
            }
        };

        void key_event(Key key, bool down);
        void mouse_button_event(MouseButton button, bool down);
        void cursor_event(Math::Vec2 position);
        void scroll_event(Math::Vec2 offset);
        void focus_event(bool focused);
        void gamepad_sample(size_t index, const std::optional<GamepadSample>& sample);
        // 返回值稳定到下次发布；复制 Frame 可供固定步／回放消费者保存。
        const Frame& publish_frame();
        [[nodiscard]] const Frame& get_frame() const { return m_frame; }

    private:
        Frame m_pending;
        Frame m_frame;
        std::array<bool, MAX_GAMEPADS> m_gamepad_baseline{};
        bool m_has_cursor_position = false;
    };
}
