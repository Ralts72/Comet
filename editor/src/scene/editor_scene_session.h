#pragma once

#include "editor_state.h"
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
        // 激活失败不得替换活动场景；成功时返回保留的 Edit 场景。
        using ActivatePlayScene =
            std::function<Comet::Result<std::unique_ptr<Comet::Scene>, Comet::Error>(
                std::unique_ptr<Comet::Scene>)>;
        // 恢复保留的 Edit 场景不重新准备资产，并返回待销毁的 Play 场景。
        using RestoreEditScene =
            std::function<std::unique_ptr<Comet::Scene>(std::unique_ptr<Comet::Scene>)>;
        using StartRuntime = std::function<Comet::Result<void, Comet::Error>()>;

        EditorSceneSession(EditorState& state, const Comet::SceneSerializer& serializer,
            ActiveSceneGetter get_active_scene, ActivatePlayScene activate_play_scene,
            RestoreEditScene restore_edit_scene, StartRuntime start_runtime);

        ~EditorSceneSession();

        EditorSceneSession(const EditorSceneSession&) = delete;
        EditorSceneSession& operator=(const EditorSceneSession&) = delete;

        void request_mode(EditorMode mode);

        [[nodiscard]] Comet::Result<bool, Comet::Error> apply_mode_request();

    private:
        [[nodiscard]] Comet::Result<bool, Comet::Error> enter_play_mode();
        [[nodiscard]] Comet::Result<bool, Comet::Error> exit_play_mode();

        EditorState& m_state;
        const Comet::SceneSerializer& m_serializer;
        ActiveSceneGetter m_get_active_scene;
        ActivatePlayScene m_activate_play_scene;
        RestoreEditScene m_restore_edit_scene;
        StartRuntime m_start_runtime;
        std::unique_ptr<Comet::Scene> m_edit_scene;
        std::optional<EditorMode> m_requested_mode;
    };
}
