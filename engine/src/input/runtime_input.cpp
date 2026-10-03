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
        m_routes = m_actions.resolve_routes(m_contexts);
        m_routes_dirty = false;
        m_actions.sample({}, m_routes, m_samples);
        m_actions.sample({}, m_routes, m_pending_samples);
        m_pending_physical = {};
        m_update = {};
        m_fixed = {};
        m_serial.reset();
        m_rebase = false;
    }

    Result<void> RuntimeInput::set_context_enabled(std::string_view name, bool enabled) {
        const auto found = std::ranges::find(m_contexts, name, &InputActions::Context::name);
        if(found == m_contexts.end())
            return Result<void>::failure("Unknown input context: " + std::string(name));
        if(found->enabled != enabled) {
            found->enabled = enabled;
            m_routes_dirty = true;
        }
        return Result<void>::success();
    }

    void RuntimeInput::rebase() {
        m_rebase = true;
        m_pending_physical.clear_transients();
        m_actions.clear_transients(m_pending_samples);
    }

    void RuntimeInput::discard() {
        static_cast<void>(consume(nullptr));
        m_actions.sample({}, m_routes, m_pending_samples);
    }

    Input::Frame RuntimeInput::consume(const Input::Frame* input) {
        Input::Frame frame;
        if(input) {
            frame = *input;
            if(m_serial && input->serial < *m_serial) {
                m_pending_physical = {};
                m_actions.sample({}, m_routes, m_pending_samples);
                m_serial.reset();
            }
            if(m_serial && input->serial == *m_serial)
                frame.clear_transients();
            m_serial = input->serial;
        }
        const auto& previous = m_pending_physical;
        if(!frame.focused)
            block_buttons(frame.keys, previous.keys);
        const bool pointer_enabled = frame.focused && frame.pointer_enabled;
        if(!pointer_enabled) {
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
        merge_buttons(pending.mouse_buttons, previous.mouse_buttons, pointer_enabled);
        for(size_t i = 0; i < pending.gamepads.size(); ++i)
            merge_buttons(pending.gamepads[i].buttons, previous.gamepads[i].buttons,
                frame.focused && pending.gamepads[i].connected);
        if(pointer_enabled) {
            accumulate(pending.cursor_delta, previous.cursor_delta);
            accumulate(pending.scroll, previous.scroll);
        }
        m_pending_physical = pending;
        return frame;
    }

    void RuntimeInput::accumulate_samples(
        InputActions::Samples& samples, const InputActions::Routing& routes) {
        for(std::size_t index = 0; index < samples.size(); ++index) {
            const auto type = m_actions.actions()[index].type;
            for(std::size_t binding = 0; binding < samples[index].size(); ++binding) {
                auto& source = samples[index][binding];
                auto& pending = m_pending_samples[index][binding];
                if(!source.available) {
                    pending = {};
                    continue;
                }
                if(!m_routes[index].test(binding) && routes[index].test(binding)) {
                    // 新获路由只建立当前电平，不重放取得路由前的点击或位移。
                    source.button.pressed = source.button.released = false;
                    if(type == InputActions::Type::Delta)
                        source.value = 0;
                    pending = source;
                    continue;
                }
                auto accumulated = source;
                if(pending.available && pending.gamepad == source.gamepad) {
                    accumulated.button.pressed |= pending.button.pressed;
                    accumulated.button.released |= pending.button.released;
                    if(type == InputActions::Type::Delta) {
                        const double total = source.value + pending.value;
                        if(std::isfinite(total))
                            accumulated.value = total;
                    }
                }
                pending = accumulated;
            }
        }
    }

    void RuntimeInput::prepare(const Input::Frame* input, bool paused) {
        auto frame = consume(input);
        InputActions::Routing changed_routes;
        if(m_routes_dirty)
            changed_routes = m_actions.resolve_routes(m_contexts);
        const auto& routes = m_routes_dirty ? changed_routes : m_routes;
        m_actions.sample(frame, routes, m_samples);
        if(paused || m_rebase) {
            if(frame.focused)
                m_rebase = false;
            // 暂停／恢复／单步只采样电平，不回放边沿与位移。
            frame.clear_transients();
            m_actions.clear_transients(m_samples);
            m_actions.evaluate_samples(frame, m_samples, m_update);
            m_update.clear_transients();
            m_fixed = m_update;
            m_pending_samples.swap(m_samples);
            m_pending_physical = frame;
        } else {
            accumulate_samples(m_samples, routes);
            m_actions.evaluate_samples(frame, m_samples, m_update);
        }
        if(m_routes_dirty) {
            m_routes = std::move(changed_routes);
            m_routes_dirty = false;
        }
    }

    const InputState& RuntimeInput::consume_fixed() {
        // 动作只消费获路由的逐绑定历史，不能从原始物理积累重演已被屏蔽的输入。
        m_actions.evaluate_samples(m_pending_physical, m_pending_samples, m_fixed);
        if(m_rebase)
            m_fixed.clear_transients();
        m_pending_physical.clear_transients();
        m_actions.clear_transients(m_pending_samples);
        return m_fixed;
    }
}
