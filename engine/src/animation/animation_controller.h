#pragma once

#include "animation/animation_data.h"
#include "common/parameters.h"
#include "asset/handle.h"

namespace Comet {
    // 共享定义只描述参数、采样和过渡；角色／物理条件由 Gameplay 提供。
    struct COMET_API AnimationController {
        struct Event {
            float phase = 0;
            std::string name;
            ParameterValue value = std::string{};
            bool operator==(const Event&) const = default;
        };
        struct State {
            std::string name;
            std::vector<AnimationBlendPoint> motions{{0, "idle"}};
            std::string parameter;
            float smoothing = 0;
            bool loop = true;
            std::vector<Event> events;
            std::string parameter_y;
            bool operator==(const State&) const = default;
        };
        struct Condition {
            enum class Compare { Equal, NotEqual, Greater, GreaterEqual, Less, LessEqual };
            std::string parameter;
            Compare compare = Compare::Equal;
            ParameterValue value = false;
            bool operator==(const Condition&) const = default;
        };
        struct Transition {
            // 空来源表示任意状态；不重新进入当前状态。顺序决定优先级。
            std::string from;
            std::string to;
            float duration = 0.15f;
            bool after_finished = false;
            std::vector<Condition> conditions;
            bool operator==(const Transition&) const = default;
        };
        struct Layer {
            std::string name;
            std::string state;
            float weight = 1;
            std::string weight_parameter;
            std::vector<std::string> mask;
            AnimationLayer::Mode mode = AnimationLayer::Mode::Override;
            std::string reference_clip;
            float reference_time = 0;
            bool operator==(const Layer&) const = default;
        };
        ParameterMap parameters;
        std::string initial = "idle";
        std::vector<State> states{{"idle"}};
        std::vector<Transition> transitions;
        AssetHandle library;
        std::vector<Layer> layers;
        [[nodiscard]] Result<AnimationBlend> sample(const State& state, Math::Vec2 value) const;
        [[nodiscard]] Result<AnimationLayer> sample_layer(
            const Layer& layer, const ParameterMap& overrides, Math::Vec2 value) const;
        [[nodiscard]] Math::Vec2 blend_parameters(
            const State& state, const ParameterMap& overrides) const;

        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] Result<void> validate_parameters(const ParameterMap& overrides) const;
        [[nodiscard]] const State* find_state(std::string_view name) const;
        [[nodiscard]] const ParameterValue* parameter(
            const std::string& name, const ParameterMap& overrides) const;
        [[nodiscard]] const Transition* next_transition(
            std::string_view from, const ParameterMap& overrides, bool finished) const;
        bool operator==(const AnimationController&) const = default;
    };
}
