#include "scene/runtime_session.h"

#include "input/input_actions.h"

#include <utility>
#include <filesystem>

namespace Comet {
    void RuntimeSession::begin(Scene& scene, ParameterMap values, std::string checkpoint) {
        end();
        m_scene = &scene;
        m_values.merge(values);
        m_checkpoint = std::move(checkpoint);
    }

    void RuntimeSession::end() noexcept {
        m_scene = nullptr;
        m_values.clear();
        m_input_context_requests.clear();
        m_scene_request.reset();
        m_checkpoint_request.reset();
        m_checkpoint.clear();
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
            || std::holds_alternative<Uuid>(value) || std::holds_alternative<Math::Vec4>(value))
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

    bool RuntimeSession::valid_values(const ParameterMap& values) {
        if(!valid_parameters(values))
            return false;
        for(const auto& [key, value] : values)
            if(std::holds_alternative<Uuid>(value) || std::holds_alternative<Math::Vec4>(value))
                return false;
        return true;
    }

    bool RuntimeSession::request_restart() {
        if(!is_active())
            return false;
        if(m_scene_request)
            return m_scene_request->kind == SceneRequest::Kind::Restart;
        m_scene_request = SceneRequest{};
        return true;
    }

    bool RuntimeSession::request_scene(std::string_view asset_path) {
        if(!is_active() || asset_path.empty() || asset_path.size() > 4096
            || asset_path.find('\0') != std::string_view::npos
            || asset_path.find('\\') != std::string_view::npos)
            return false;
        const std::filesystem::path path(asset_path);
        if(path.is_absolute() || path.has_root_name() || path.extension() != ".scene")
            return false;
        for(const auto& part : path)
            if(part == "..")
                return false;
        const auto normalized = path.lexically_normal().generic_string();
        if(m_scene_request)
            return m_scene_request->kind == SceneRequest::Kind::Load
                   && m_scene_request->path == normalized;
        m_scene_request = SceneRequest{SceneRequest::Kind::Load, normalized, {}};
        return true;
    }

    bool RuntimeSession::request_continue() {
        if(!is_active())
            return false;
        if(m_scene_request)
            return m_scene_request->kind == SceneRequest::Kind::Continue;
        m_scene_request = SceneRequest{SceneRequest::Kind::Continue, {}, {}, {}};
        return true;
    }

    bool RuntimeSession::request_checkpoint(
        std::string_view marker, std::span<const std::string_view> keys) {
        if(!is_active() || !valid_parameter_name(marker) || keys.size() > 128)
            return false;
        CheckpointRequest request{std::string(marker), {}};
        for(const auto key : keys) {
            const auto found = m_values.find(key);
            if(found == m_values.end()
                || !request.values.emplace(found->first, found->second).second)
                return false;
        }
        if(m_checkpoint_request)
            return m_checkpoint_request->marker == request.marker
                   && m_checkpoint_request->values == request.values;
        m_checkpoint_request = std::move(request);
        return true;
    }

    std::optional<RuntimeSession::CheckpointRequest> RuntimeSession::take_checkpoint_request() {
        return std::exchange(m_checkpoint_request, std::nullopt);
    }

    std::optional<RuntimeSession::SceneRequest> RuntimeSession::take_scene_request() {
        auto request = std::exchange(m_scene_request, std::nullopt);
        if(request && request->kind == SceneRequest::Kind::Load)
            request->values.insert(m_values.begin(), m_values.end());
        return request;
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
