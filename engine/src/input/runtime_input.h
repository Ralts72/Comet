#pragma once

#include "input/input_actions.h"

#include <optional>

namespace Comet {
    // 一个运行域的输入消费状态；调度者只提供授权帧和阶段，不操作内部按钮／动作缓存。
    class COMET_API RuntimeInput final {
    public:
        void configure(InputActions actions);
        void reset();
        void rebase();
        void discard();
        void prepare(const Input::Frame* input, bool paused = false);
        [[nodiscard]] const InputState& update() const { return m_update; }
        [[nodiscard]] const InputState& consume_fixed();

    private:
        Input::Frame consume(const Input::Frame* input);

        InputActions m_actions;
        InputState m_update;
        InputState m_fixed;
        Input::Frame m_pending_fixed;
        std::optional<uint64_t> m_serial;
        bool m_rebase = false;
    };
}
