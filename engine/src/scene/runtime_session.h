#pragma once

#include "common/export.h"
#include "common/parameters.h"

#include <map>
#include <span>
#include <optional>
#include <string>
#include <string_view>

namespace Comet {
    class Scene;
    class SceneRuntime;

    // 一次运行的共享状态与控制请求；由 SceneRuntime 启停，不属于场景内容。
    class COMET_API RuntimeSession final {
    public:
        struct SceneRequest {
            enum class Kind { Restart, Load, Continue } kind = Kind::Restart;
            std::string path;
            ParameterMap values;
            std::string checkpoint;
        };
        struct CheckpointRequest {
            std::string marker;
            ParameterMap values;
        };
        RuntimeSession() = default;
        RuntimeSession(const RuntimeSession&) = delete;
        RuntimeSession& operator=(const RuntimeSession&) = delete;

        [[nodiscard]] bool is_active() const { return m_scene != nullptr; }
        [[nodiscard]] bool is_bound_to(const Scene& scene) const { return m_scene == &scene; }
        [[nodiscard]] std::optional<ParameterValue> get_value(std::string_view key) const;
        [[nodiscard]] bool set_value(std::string_view key, ParameterValue value);
        [[nodiscard]] bool erase_value(std::string_view key);
        // 同帧首个请求优先；同一请求可合并，由宿主在更新结束后消费。
        [[nodiscard]] bool request_restart();
        [[nodiscard]] bool request_scene(std::string_view asset_path);
        [[nodiscard]] bool request_continue();
        [[nodiscard]] bool request_checkpoint(
            std::string_view marker, std::span<const std::string_view> keys);
        [[nodiscard]] std::string_view checkpoint() const { return m_checkpoint; }
        [[nodiscard]] static bool valid_values(const ParameterMap& values);
        // 同名请求合并，在下一次 Runtime 输入准备时生效。
        [[nodiscard]] bool request_input_context(std::string_view name, bool enabled);

    private:
        friend class SceneRuntime;
        using InputContextRequests = std::map<std::string, bool, std::less<>>;
        void begin(Scene& scene, ParameterMap values, std::string checkpoint);
        void end() noexcept;
        [[nodiscard]] std::optional<SceneRequest> take_scene_request();
        [[nodiscard]] InputContextRequests take_input_context_requests();
        [[nodiscard]] std::optional<CheckpointRequest> take_checkpoint_request();

        Scene* m_scene = nullptr;
        std::map<std::string, ParameterValue, std::less<>> m_values;
        InputContextRequests m_input_context_requests;
        std::optional<SceneRequest> m_scene_request;
        std::optional<CheckpointRequest> m_checkpoint_request;
        std::string m_checkpoint;
    };
}
