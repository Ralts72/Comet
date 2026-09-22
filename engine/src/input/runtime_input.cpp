#include "input/runtime_input.h"

#include <utility>

namespace Comet {
    namespace {
        void block_buttons(auto& buttons, const auto& previous) {
            for(size_t i = 0; i < buttons.size(); ++i)
                buttons[i] = {.released = buttons[i].released || previous[i].down};
        }

        void merge_buttons(auto& buttons, const auto& previous, bool accept_press) {
            for(size_t i = 0; i < buttons.size(); ++i) {
                if(accept_press)
                    buttons[i].pressed |= previous[i].pressed;
                buttons[i].released |= previous[i].released;
            }
        }

        void accumulate(Math::Vec2& target, const Math::Vec2 value) {
            if(Math::is_finite(value) && Math::is_finite(target + value))
                target += value;
        }
    }

    void RuntimeInput::configure(InputActions actions) {
        m_actions = std::move(actions);
        reset();
    }

    void RuntimeInput::reset() {
        m_update = {};
        m_fixed = {};
        m_pending_fixed = {};
        m_serial.reset();
        m_rebase = false;
    }

    void RuntimeInput::rebase() {
        m_rebase = true;
        m_pending_fixed.clear_transients();
    }

    void RuntimeInput::discard() {
        static_cast<void>(consume(nullptr));
    }

    Input::Frame RuntimeInput::consume(const Input::Frame* input) {
        Input::Frame frame;
        if(input) {
            frame = *input;
            if(m_serial && input->serial < *m_serial) {
                m_pending_fixed = {};
                m_serial.reset();
            }
            if(m_serial && input->serial == *m_serial)
                frame.clear_transients();
            m_serial = input->serial;
        }
        if(!frame.focused) {
            block_buttons(frame.keys, m_pending_fixed.keys);
            block_buttons(frame.mouse_buttons, m_pending_fixed.mouse_buttons);
            frame.cursor_delta = {};
            frame.scroll = {};
        }
        for(size_t i = 0; i < frame.gamepads.size(); ++i) {
            auto& pad = frame.gamepads[i];
            if(!frame.focused || !pad.connected) {
                block_buttons(pad.buttons, m_pending_fixed.gamepads[i].buttons);
                pad.axes.fill(0);
            }
        }
        auto pending = frame;
        merge_buttons(pending.keys, m_pending_fixed.keys, frame.focused);
        merge_buttons(pending.mouse_buttons, m_pending_fixed.mouse_buttons, frame.focused);
        for(size_t i = 0; i < pending.gamepads.size(); ++i)
            merge_buttons(pending.gamepads[i].buttons, m_pending_fixed.gamepads[i].buttons,
                frame.focused && pending.gamepads[i].connected);
        if(frame.focused) {
            accumulate(pending.cursor_delta, m_pending_fixed.cursor_delta);
            accumulate(pending.scroll, m_pending_fixed.scroll);
        }
        m_pending_fixed = pending;
        return frame;
    }

    void RuntimeInput::prepare(const Input::Frame* input, bool paused) {
        auto frame = consume(input);
        if(paused || m_rebase) {
            m_rebase = false;
            // 暂停／恢复／单步只采样电平，不回放边沿与位移。
            frame.clear_transients();
            m_pending_fixed = frame;
            m_actions.evaluate(frame, m_fixed);
            m_fixed.clear_transients();
            m_actions.evaluate(frame, m_update);
            m_update.clear_transients();
        } else {
            m_actions.evaluate(frame, m_update);
        }
    }

    const InputState& RuntimeInput::consume_fixed() {
        m_actions.evaluate(m_pending_fixed, m_fixed);
        m_pending_fixed.clear_transients();
        return m_fixed;
    }
}
