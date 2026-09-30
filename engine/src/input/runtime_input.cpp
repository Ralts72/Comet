#include "input/runtime_input.h"

#include <algorithm>
#include <cmath>
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
        m_contexts = m_actions.contexts();
        m_changed_contexts.clear();
        m_update = {};
        m_fixed = {};
        m_pending_fixed = {};
        m_serial.reset();
        m_rebase = false;
    }

    Result<void> RuntimeInput::set_context_enabled(std::string_view name, bool enabled) {
        const auto found = std::ranges::find(m_contexts, name, &InputActions::Context::name);
        if(found == m_contexts.end())
            return Result<void>::failure("Unknown input context: " + std::string(name));
        if(found->enabled != enabled) {
            found->enabled = enabled;
            m_changed_contexts.insert(found->name);
        }
        return Result<void>::success();
    }

    void RuntimeInput::rebase() {
        m_rebase = true;
        m_pending_fixed.clear_transients();
    }

    void RuntimeInput::discard() {
        static_cast<void>(consume(nullptr));
        for(auto& [name, action] : m_pending_fixed.m_actions) {
            const bool released = action.released || action.down;
            action = {.type = action.type, .released = released};
        }
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
        const auto& previous = m_pending_fixed.m_physical;
        if(!frame.focused) {
            block_buttons(frame.keys, previous.keys);
            block_buttons(frame.mouse_buttons, previous.mouse_buttons);
            frame.cursor_delta = {};
            frame.scroll = {};
        }
        for(size_t i = 0; i < frame.gamepads.size(); ++i) {
            auto& pad = frame.gamepads[i];
            if(!frame.focused || !pad.connected) {
                block_buttons(pad.buttons, previous.gamepads[i].buttons);
                pad.axes.fill(0);
            }
        }
        auto pending = frame;
        merge_buttons(pending.keys, previous.keys, frame.focused);
        merge_buttons(pending.mouse_buttons, previous.mouse_buttons, frame.focused);
        for(size_t i = 0; i < pending.gamepads.size(); ++i)
            merge_buttons(pending.gamepads[i].buttons, previous.gamepads[i].buttons,
                frame.focused && pending.gamepads[i].connected);
        if(frame.focused) {
            accumulate(pending.cursor_delta, previous.cursor_delta);
            accumulate(pending.scroll, previous.scroll);
        }
        m_pending_fixed.m_physical = pending;
        return frame;
    }

    void RuntimeInput::accumulate_actions(const Input::Frame& frame) {
        // Fixed 尚未消费的按下不能被 Update 自己的上一帧电平抑制。
        InputState sampled;
        m_actions.evaluate(frame, sampled, m_contexts);
        for(const auto& action : m_actions.actions()) {
            auto& current = m_update.m_actions.at(action.name);
            auto& pending = m_pending_fixed.m_actions[action.name];
            auto accumulated = sampled.m_actions.at(action.name);
            if(m_changed_contexts.contains(action.context)) {
                const auto context =
                    std::ranges::find(m_contexts, action.context, &InputActions::Context::name);
                if(context->enabled) {
                    // 新组只接管当前电平，不能接收启用前的点击或位移。
                    current.pressed = current.released = false;
                    if(current.type == InputActions::Type::Delta)
                        current.value = 0;
                }
                if(context->enabled)
                    accumulated = current;
                pending = accumulated;
                if(!context->enabled && accumulated.type == InputActions::Type::Button) {
                    const auto* previous = m_fixed.action(action.name);
                    pending.released |= previous && previous->down;
                }
                continue;
            }
            if(accumulated.type == InputActions::Type::Button) {
                if(m_update.focused())
                    accumulated.pressed |= pending.pressed;
                accumulated.released |= pending.released;
                const auto* previous = m_fixed.action(action.name);
                accumulated.released |= previous && previous->down && !accumulated.down;
            } else if(accumulated.type == InputActions::Type::Delta && m_update.focused()) {
                const auto total = accumulated.value + pending.value;
                if(std::isfinite(total))
                    accumulated.value = total;
            }
            pending = accumulated;
        }
        m_changed_contexts.clear();
    }

    void RuntimeInput::prepare(const Input::Frame* input, bool paused) {
        auto frame = consume(input);
        if(paused || m_rebase) {
            if(frame.focused)
                m_rebase = false;
            // 暂停／恢复／单步只采样电平，不回放边沿与位移。
            frame.clear_transients();
            m_actions.evaluate(frame, m_update, m_contexts);
            m_update.clear_transients();
            m_fixed = m_pending_fixed = m_update;
            m_changed_contexts.clear();
        } else {
            m_actions.evaluate(frame, m_update, m_contexts);
            accumulate_actions(frame);
        }
    }

    const InputState& RuntimeInput::consume_fixed() {
        m_actions.evaluate(m_pending_fixed.m_physical, m_fixed, m_contexts);
        for(const auto& [name, pending] : m_pending_fixed.m_actions) {
            auto& action = m_fixed.m_actions.at(name);
            if(action.type == InputActions::Type::Button) {
                action.pressed &= pending.pressed;
                action.released &= pending.released;
            } else if(action.type == InputActions::Type::Delta) {
                action.value = pending.value;
            }
        }
        m_pending_fixed.clear_transients();
        return m_fixed;
    }
}
