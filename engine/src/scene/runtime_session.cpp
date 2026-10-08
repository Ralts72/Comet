#include "scene/runtime_session.h"

#include "input/input_actions.h"

#include <utility>

namespace Comet {
    void RuntimeSession::begin(Scene& scene) {
        end();
        m_scene = &scene;
    }

    void RuntimeSession::end() noexcept {
        m_scene = nullptr;
        m_values.clear();
        m_input_context_requests.clear();
        m_restart_requested = false;
    }

    std::optional<ParameterValue> RuntimeSession::get_value(const std::string_view key) const {
        if(!is_active() || !valid_parameter_name(key))
            return std::nullopt;
        const auto found = m_values.find(key);
        if(found == m_values.end())
            return std::nullopt;
        return found->second;
    }

    bool RuntimeSession::set_value(const std::string_view key, ParameterValue value) {
        if(!is_active() || !valid_parameter_name(key) || !valid_parameter_value(value)
            || std::holds_alternative<EntityUuid>(value)
            || std::holds_alternative<Math::Vec4>(value))
            return false;
        if(m_values.size() >= 128 && !m_values.contains(key))
            return false;
        m_values.insert_or_assign(std::string(key), std::move(value));
        return true;
    }

    bool RuntimeSession::erase_value(const std::string_view key) {
        if(!is_active() || !valid_parameter_name(key))
            return false;
        const auto found = m_values.find(key);
        if(found != m_values.end())
            m_values.erase(found);
        return true;
    }

    bool RuntimeSession::request_restart() {
        if(!is_active())
            return false;
        m_restart_requested = true;
        return true;
    }

    bool RuntimeSession::take_restart_request() {
        return std::exchange(m_restart_requested, false);
    }

    bool RuntimeSession::request_input_context(const std::string_view name, const bool enabled) {
        if(!is_active() || !InputActions::valid_name(name))
            return false;
        if(m_input_context_requests.size() >= InputActions::MAX_CONTEXTS
            && !m_input_context_requests.contains(name))
            return false;
        m_input_context_requests.insert_or_assign(std::string(name), enabled);
        return true;
    }

    RuntimeSession::InputContextRequests RuntimeSession::take_input_context_requests() {
        return std::exchange(m_input_context_requests, {});
    }
}
