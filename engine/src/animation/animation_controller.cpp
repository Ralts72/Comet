#include "animation/animation_controller.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Comet {
    namespace {
        bool scalar(const ParameterValue& value) {
            if(const auto* number = std::get_if<float>(&value))
                return std::isfinite(*number);
            return std::holds_alternative<bool>(value);
        }
        bool matches(const AnimationController::Condition& condition, const ParameterValue& value) {
            using Compare = AnimationController::Condition::Compare;
            if(condition.compare == Compare::Equal)
                return value == condition.value;
            if(condition.compare == Compare::NotEqual)
                return value != condition.value;
            const auto* first = std::get_if<float>(&value);
            const auto* second = std::get_if<float>(&condition.value);
            if(!first || !second)
                return false;
            switch(condition.compare) {
                case Compare::Greater:
                    return *first > *second;
                case Compare::GreaterEqual:
                    return *first >= *second;
                case Compare::Less:
                    return *first < *second;
                case Compare::LessEqual:
                    return *first <= *second;
                default:
                    return false;
            }
        }
    }
    const AnimationController::State* AnimationController::find_state(std::string_view name) const {
        const auto found = std::ranges::find(states, name, &State::name);
        return found == states.end() ? nullptr : &*found;
    }
    const ParameterValue* AnimationController::parameter(
        const std::string& name, const ParameterMap& overrides) const {
        if(const auto found = overrides.find(name); found != overrides.end())
            return &found->second;
        const auto found = parameters.find(name);
        return found == parameters.end() ? nullptr : &found->second;
    }
    Result<void> AnimationController::validate_parameters(const ParameterMap& overrides) const {
        for(const auto& [name, value] : overrides) {
            const auto found = parameters.find(name);
            if(found == parameters.end() || found->second.index() != value.index()
                || !scalar(value))
                return Result<void>::failure("Unknown or mistyped animation parameter: " + name);
        }
        return Result<void>::success();
    }
    Result<void> AnimationController::validate() const {
        if(states.empty() || states.size() > 128 || transitions.size() > 512
            || parameters.size() > 128 || !valid_parameters(parameters) || !find_state(initial))
            return Result<void>::failure("Invalid animation controller or initial state");
        for(const auto& [name, value] : parameters)
            if(!scalar(value))
                return Result<void>::failure(
                    "Animation parameter must be a boolean or number: " + name);
        std::unordered_set<std::string> names;
        for(const auto& state : states) {
            if(!valid_parameter_name(state.name) || !names.insert(state.name).second
                || !std::isfinite(state.smoothing) || state.smoothing < 0 || state.motions.empty()
                || state.motions.size() > 64)
                return Result<void>::failure("Invalid animation state: " + state.name);
            if(state.parameter.empty()) {
                if(state.motions.size() != 1)
                    return Result<void>::failure(
                        "Blend state requires a numeric parameter: " + state.name);
            } else {
                const auto* value = parameter(state.parameter, {});
                if(!value || !std::holds_alternative<float>(*value))
                    return Result<void>::failure(
                        "Unknown numeric blend parameter: " + state.parameter);
            }
            if(auto blend = blend_animation_1d(state.motions, 0); !blend)
                return Result<void>::failure(blend.error());
            for(const auto& motion : state.motions)
                if(motion.clip.empty())
                    return Result<void>::failure("Animation state needs a clip: " + state.name);
        }
        for(const auto& transition : transitions) {
            if((!transition.from.empty() && !find_state(transition.from))
                || !find_state(transition.to) || transition.from == transition.to
                || !std::isfinite(transition.duration) || transition.duration < 0
                || transition.conditions.size() > 128)
                return Result<void>::failure("Invalid animation transition to: " + transition.to);
            for(const auto& condition : transition.conditions) {
                const auto* value = parameter(condition.parameter, {});
                using Compare = Condition::Compare;
                const auto op = condition.compare;
                if(!value || value->index() != condition.value.index() || !scalar(condition.value)
                    || op < Compare::Equal || op > Compare::LessEqual
                    || (std::holds_alternative<bool>(*value) && op != Compare::Equal
                        && op != Compare::NotEqual))
                    return Result<void>::failure(
                        "Invalid animation condition: " + condition.parameter);
            }
        }
        return Result<void>::success();
    }
    const AnimationController::Transition* AnimationController::next_transition(
        std::string_view from, const ParameterMap& overrides, bool finished) const {
        for(const auto& transition : transitions) {
            if(transition.to == from || (!transition.from.empty() && transition.from != from)
                || (transition.after_finished && !finished))
                continue;
            if(std::ranges::all_of(transition.conditions, [&](const auto& condition) {
                   const auto* value = parameter(condition.parameter, overrides);
                   return value && matches(condition, *value);
               }))
                return &transition;
        }
        return nullptr;
    }
}
