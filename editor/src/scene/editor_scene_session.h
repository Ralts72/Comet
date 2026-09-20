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
        // 替换前由场景所有者停止旧场景的 Runtime。
        using ActiveSceneReplacer =
            std::function<std::unique_ptr<Comet::Scene>(std::unique_ptr<Comet::Scene>, EditorMode)>;
        using StartRuntime = std::function<Comet::Result<void, Comet::Error>()>;
        using PrepareCandidate = std::function<Comet::Result<void, Comet::Error>(Comet::Scene&)>;

        EditorSceneSession(EditorState& state, const Comet::SceneSerializer& serializer,
            ActiveSceneGetter get_active_scene, ActiveSceneReplacer replace_active_scene,
            StartRuntime start_runtime, PrepareCandidate prepare_candidate = {});

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
        ActiveSceneReplacer m_replace_active_scene;
        StartRuntime m_start_runtime;
        PrepareCandidate m_prepare_candidate;
        std::unique_ptr<Comet::Scene> m_edit_scene;
        std::optional<EditorMode> m_requested_mode;
    };
}
