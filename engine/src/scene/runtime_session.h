#pragma once

#include "common/export.h"
#include "scene/property.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Comet {
    class Scene;
    class SceneRuntime;

    // 一次运行的共享状态与控制请求；由 SceneRuntime 启停，不属于场景内容。
    class COMET_API RuntimeSession final {
    public:
        RuntimeSession() = default;
        RuntimeSession(const RuntimeSession&) = delete;
        RuntimeSession& operator=(const RuntimeSession&) = delete;

        [[nodiscard]] bool is_active() const { return m_scene != nullptr; }
        [[nodiscard]] bool is_bound_to(const Scene& scene) const { return m_scene == &scene; }
        [[nodiscard]] std::optional<ParameterValue> get_value(std::string_view key) const;
        [[nodiscard]] bool set_value(std::string_view key, ParameterValue value);
        [[nodiscard]] bool erase_value(std::string_view key);
        // 合并重开意图，由宿主在运行更新结束后消费。
        [[nodiscard]] bool request_restart();
        // 同名请求合并，在下一次 Runtime 输入准备时生效。
        [[nodiscard]] bool request_input_context(std::string_view name, bool enabled);

    private:
        friend class SceneRuntime;
        using InputContextRequests = std::map<std::string, bool, std::less<>>;
        void begin(Scene& scene);
        void end() noexcept;
        [[nodiscard]] bool take_restart_request();
        [[nodiscard]] InputContextRequests take_input_context_requests();

        Scene* m_scene = nullptr;
        std::map<std::string, ParameterValue, std::less<>> m_values;
        InputContextRequests m_input_context_requests;
        bool m_restart_requested = false;
    };
}
