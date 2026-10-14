#pragma once

#include "editor_state.h"
#include "scene/scene_runtime.h"
#include "gameplay/game_session.h"
#include "common/result.h"
#include "common/error.h"

#include <functional>
#include <memory>
#include <optional>

namespace Comet {
    class Scene;
    class SceneSerializer;
}

namespace CometEditor {
    class EditorSceneSession final {
    public:
        using ActiveSceneGetter = std::function<Comet::Scene*()>;
        // 激活失败不得替换活动场景；成功时返回之前的 Edit 或 Play 场景。
        using ActivatePlayScene =
            std::function<Comet::Result<std::unique_ptr<Comet::Scene>, Comet::Error>(
                std::unique_ptr<Comet::Scene>)>;
        // 恢复保留的 Edit 场景不重新准备资产，并返回待销毁的 Play 场景。
        using RestoreEditScene =
            std::function<std::unique_ptr<Comet::Scene>(std::unique_ptr<Comet::Scene>)>;
        using TakeSceneRequest =
            std::function<std::optional<Comet::RuntimeSession::SceneRequest>()>;
        using StartRuntime = std::function<Comet::Result<void, Comet::Error>(
            Comet::SceneRuntime::State, const Comet::ParameterMap&, std::string_view)>;

        EditorSceneSession(EditorState& state, const Comet::SceneSerializer& serializer,
            ActiveSceneGetter get_active_scene, ActivatePlayScene activate_play_scene,
            RestoreEditScene restore_edit_scene, StartRuntime start_runtime,
            TakeSceneRequest take_scene_request = {},
            Comet::GameSession::LoadScene load_scene = {});

        ~EditorSceneSession();

        EditorSceneSession(const EditorSceneSession&) = delete;
        EditorSceneSession& operator=(const EditorSceneSession&) = delete;

        using TakeCheckpointRequest =
            std::function<std::optional<Comet::RuntimeSession::CheckpointRequest>()>;
        void set_persistence(Comet::GameSession::Persistence persistence,
            TakeCheckpointRequest take_request, std::function<std::string()> scene_path);

        void request_mode(EditorMode mode);

        [[nodiscard]] Comet::Result<bool, Comet::Error> apply_mode_request(
            Comet::SceneRuntime::State restart_state = Comet::SceneRuntime::State::Running);

    private:
        [[nodiscard]] Comet::Result<bool, Comet::Error> start_play_mode(
            Comet::SceneRuntime::State initial_state);
        [[nodiscard]] Comet::Result<bool, Comet::Error> transition(
            const Comet::RuntimeSession::SceneRequest& request, Comet::SceneRuntime::State state);
        [[nodiscard]] Comet::Result<bool, Comet::Error> exit_play_mode();

        EditorState& m_state;
        Comet::GameSession m_game;
        ActiveSceneGetter m_get_active_scene;
        ActivatePlayScene m_activate_play_scene;
        RestoreEditScene m_restore_edit_scene;
        StartRuntime m_start_runtime;
        TakeSceneRequest m_take_scene_request;
        Comet::GameSession::LoadScene m_load_scene;
        TakeCheckpointRequest m_take_checkpoint_request;
        std::function<std::string()> m_scene_path;
        std::unique_ptr<Comet::Scene> m_edit_scene;
        std::optional<EditorMode> m_requested_mode;
    };
}
