#pragma once

#include "core/project_paths.h"
#include "common/result.h"
#include "common/error.h"
#include "scene/command_history.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Comet {
    class Scene;
    class SceneSerializer;
}

namespace CometEditor {
    class SceneDocument final {
    public:
        enum class Action { New, Open, Close };
        enum class Decision { Save, Discard, Cancel };
        struct Request {
            Action action;
            std::string path;
        };
        using ActiveSceneGetter = std::function<Comet::Scene*()>;
        // 失败时不得替换活动场景；文档路径和保存点只在成功后更新。
        using ActivateScene =
            std::function<Comet::Result<void, Comet::Error>(std::unique_ptr<Comet::Scene>)>;

        SceneDocument(const Comet::SceneSerializer& serializer, Comet::ProjectPaths paths,
            const CommandHistory& history, ActiveSceneGetter get_active_scene,
            ActivateScene activate_scene);

        [[nodiscard]] Comet::Result<void, Comet::Error> create_new();
        [[nodiscard]] Comet::Result<void, Comet::Error> open(const std::string& path);
        [[nodiscard]] Comet::Result<void, Comet::Error> save(const std::string& path);

        [[nodiscard]] const std::string& get_path() const noexcept { return m_path; }
        [[nodiscard]] const std::filesystem::path& get_asset_relative_path() const noexcept {
            return m_asset_relative_path;
        }
        [[nodiscard]] const std::string& get_last_error() const noexcept { return m_last_error; }
        void clear_error() noexcept { m_last_error.clear(); }
        [[nodiscard]] bool is_modified() const { return m_saved_state != m_history.state_id(); }
        void request(Request request);
        void decide(Decision decision);
        [[nodiscard]] bool has_pending_request() const { return m_pending_request.has_value(); }
        [[nodiscard]] bool needs_confirmation() const;
        [[nodiscard]] std::optional<Request> take_ready_request();

    private:
        [[nodiscard]] Comet::Result<void, Comet::Error> activate_scene(
            std::unique_ptr<Comet::Scene> scene, std::string path);

        const Comet::SceneSerializer& m_serializer;
        const CommandHistory& m_history;
        std::uint64_t m_saved_state;
        Comet::ProjectPaths m_paths;
        ActiveSceneGetter m_get_active_scene;
        ActivateScene m_activate_scene;
        std::string m_path;
        std::filesystem::path m_asset_relative_path;
        std::string m_last_error;
        enum class PendingState { Confirm, Saving, Discard };
        struct PendingRequest {
            Request action;
            PendingState state = PendingState::Confirm;
        };
        std::optional<PendingRequest> m_pending_request;
    };
}
