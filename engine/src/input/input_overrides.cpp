#include "input/input_overrides.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace Comet {
    Result<InputOverrides> InputOverrides::create(std::vector<Action> actions) {
        using Created = Result<InputOverrides>;
        if(actions.size() > InputActions::MAX_ACTIONS)
            return Created::failure("Too many input action overrides");
        std::set<Uuid> action_ids;
        for(const auto& action : actions) {
            if(!action.id || !action_ids.insert(action.id).second)
                return Created::failure("Invalid or duplicate input override action ID");
            if(auto valid = InputActions::create({{"override", action.type, {}}}); !valid)
                return Created::failure(valid.error());
            if(action.disabled ? !action.bindings.empty() : action.bindings.empty())
                return Created::failure(
                    "Input action override requires either disabled or binding overrides");
            if(action.bindings.size() > InputActions::MAX_BINDINGS)
                return Created::failure("Too many input binding overrides");
            std::set<Uuid> binding_ids;
            for(const auto& binding : action.bindings) {
                if(!binding.id || !binding_ids.insert(binding.id).second)
                    return Created::failure("Invalid or duplicate input override binding ID");
                const bool has_fields = binding.control || binding.scale || binding.deadzone;
                if(binding.disabled ? has_fields : !has_fields)
                    return Created::failure(
                        "Input binding override requires either disabled or changed fields");
                if((binding.scale && !std::isfinite(*binding.scale))
                    || (binding.deadzone && !std::isfinite(*binding.deadzone)))
                    return Created::failure("Input override values must be finite");
                if(binding.control) {
                    if(auto valid = InputActions::format_binding({*binding.control}); !valid)
                        return Created::failure(valid.error());
                }
            }
        }
        InputOverrides result;
        result.m_actions = std::move(actions);
        return Created::success(std::move(result));
    }

    Result<InputOverrides::Resolution> InputOverrides::resolve(const InputActions& defaults) const {
        using Resolved = Result<Resolution>;
        if(auto valid = defaults.validate_persistent_ids(); !valid)
            return Resolved::failure(valid.error());
        auto actions = defaults.actions();
        std::vector<Issue> issues;
        for(const auto& patch : m_actions) {
            const auto action = std::ranges::find(actions, patch.id, &InputActions::Action::id);
            if(action == actions.end()) {
                issues.push_back({patch.id, {}, "Unknown input action ID; override ignored"});
                continue;
            }
            if(action->type != patch.type) {
                issues.push_back({patch.id, {}, "Input action type changed; override ignored"});
                continue;
            }
            if(patch.disabled) {
                action->bindings.clear();
                continue;
            }
            for(const auto& binding_patch : patch.bindings) {
                const auto binding = std::ranges::find(
                    action->bindings, binding_patch.id, &InputActions::Binding::id);
                if(binding == action->bindings.end()) {
                    issues.push_back(
                        {patch.id, binding_patch.id, "Unknown input binding ID; override ignored"});
                    continue;
                }
                if(binding_patch.disabled) {
                    action->bindings.erase(binding);
                    continue;
                }
                auto candidate = *binding;
                if(binding_patch.control)
                    candidate.control = *binding_patch.control;
                if(binding_patch.scale)
                    candidate.scale = *binding_patch.scale;
                if(binding_patch.deadzone)
                    candidate.deadzone = *binding_patch.deadzone;
                const auto valid = InputActions::create(
                    {{action->name, action->type, {candidate}, action->context, action->id}},
                    defaults.contexts());
                if(!valid) {
                    issues.push_back({patch.id, binding_patch.id,
                        valid.error() + "; keeping the default binding"});
                    continue;
                }
                *binding = std::move(candidate);
            }
        }
        auto resolved = InputActions::create(std::move(actions), defaults.contexts());
        if(!resolved)
            return Resolved::failure(resolved.error());
        return Resolved::success({std::move(resolved).value(), std::move(issues)});
    }
}
