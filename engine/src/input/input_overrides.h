#pragma once

#include "input/input_actions.h"

#include <optional>

namespace Comet {
    // 按持久身份保存玩家意图；合成结果不回写为覆盖记录。
    class COMET_API InputOverrides final {
    public:
        struct Binding {
            Uuid id;
            std::optional<InputActions::Control> control;
            std::optional<float> scale;
            std::optional<float> deadzone;
            bool disabled = false;
            bool operator==(const Binding&) const = default;
        };
        struct Action {
            Uuid id;
            InputActions::Type type = InputActions::Type::Button;
            bool disabled = false;
            std::vector<Binding> bindings;
            bool operator==(const Action&) const = default;
        };
        struct Issue {
            Uuid action;
            Uuid binding;
            std::string message;
        };
        struct Resolution {
            InputActions actions;
            std::vector<Issue> issues;
        };

        [[nodiscard]] static Result<InputOverrides> create(std::vector<Action> actions);
        [[nodiscard]] const std::vector<Action>& actions() const { return m_actions; }
        // 不兼容的记录保留原样，只有对应动作或绑定退回当前默认。
        [[nodiscard]] Result<Resolution> resolve(const InputActions& defaults) const;
        bool operator==(const InputOverrides&) const = default;

    private:
        std::vector<Action> m_actions;
    };
}
