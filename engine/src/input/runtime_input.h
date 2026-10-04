#pragma once

#include "input/input_actions.h"

#include <optional>

namespace Comet {
    // 一个运行域的输入消费状态；调度者只提供授权帧和阶段，不操作内部按钮／动作缓存。
    class COMET_API RuntimeInput final {
    public:
        void configure(InputActions actions);
        // 仅绑定可变；下一次 prepare 应用，重复请求取最后一次，reset 取消未应用请求。
        [[nodiscard]] Result<void> request_rebind(InputActions actions);
        [[nodiscard]] Result<void> set_context_enabled(std::string_view name, bool enabled);
        void reset();
        void rebase();
        void discard();
        void prepare(const Input::Frame* input, bool paused = false);
        [[nodiscard]] const InputState& update() const { return m_update; }
        [[nodiscard]] const InputState& consume_fixed();

    private:
        Input::Frame consume(const Input::Frame* input);
        void apply_rebind();
        void accumulate_samples(
            InputActions::Samples& samples, const InputActions::Routing& routes);

        InputActions m_actions;
        std::optional<InputActions> m_pending_actions;
        std::vector<InputActions::Context> m_contexts;
        InputActions::Routing m_routes;
        // 新绑定／新获路由须等实际可用的采样才能完成基线。
        InputActions::Routing m_binding_baselines;
        // prepare 工作缓冲；每次采样覆盖全部槽位，不保存消费历史。
        InputActions::Samples m_samples;
        InputActions::Samples m_pending_samples;
        Input::Frame m_pending_physical;
        InputState m_update;
        InputState m_fixed;
        std::optional<uint64_t> m_serial;
        bool m_routes_dirty = false;
        bool m_rebase = false;
    };
}
