#pragma once

#include "editor_state.h"

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
        using ActiveSceneReplacer = std::function<std::unique_ptr<Comet::Scene>(
            std::unique_ptr<Comet::Scene>)>;

        EditorSceneSession(
            EditorState& state,
            const Comet::SceneSerializer& serializer,
            ActiveSceneGetter get_active_scene,
            ActiveSceneReplacer replace_active_scene);

        ~EditorSceneSession();

        EditorSceneSession(const EditorSceneSession&) = delete;
        EditorSceneSession& operator=(const EditorSceneSession&) = delete;

        void request_mode(EditorMode mode);

        [[nodiscard]] bool has_pending_mode_request() const {
            return m_requested_mode.has_value();
        }

        [[nodiscard]] bool apply_mode_request();

    private:
        [[nodiscard]] bool enter_play_mode();
        [[nodiscard]] bool exit_play_mode();

        EditorState& m_state;
        const Comet::SceneSerializer& m_serializer;
        ActiveSceneGetter m_get_active_scene;
        ActiveSceneReplacer m_replace_active_scene;
        std::unique_ptr<Comet::Scene> m_edit_scene;
        std::optional<EditorMode> m_requested_mode;
    };
}
