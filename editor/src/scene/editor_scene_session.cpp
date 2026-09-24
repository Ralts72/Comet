#include "scene/editor_scene_session.h"

#include "diagnostics/logger.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <utility>

namespace CometEditor {
    EditorSceneSession::EditorSceneSession(EditorState& state,
        const Comet::SceneSerializer& serializer, ActiveSceneGetter get_active_scene,
        ActivatePlayScene activate_play_scene, RestoreEditScene restore_edit_scene,
        StartRuntime start_runtime)
        : m_state(state), m_serializer(serializer), m_get_active_scene(std::move(get_active_scene)),
          m_activate_play_scene(std::move(activate_play_scene)),
          m_restore_edit_scene(std::move(restore_edit_scene)),
          m_start_runtime(std::move(start_runtime)) {}

    EditorSceneSession::~EditorSceneSession() = default;

    void EditorSceneSession::request_mode(const EditorMode mode) {
        if(mode == m_state.mode) {
            m_requested_mode.reset();
            return;
        }
        m_requested_mode = mode;
    }

    Comet::Result<bool, Comet::Error> EditorSceneSession::apply_mode_request() {
        if(!m_requested_mode) {
            return Comet::Result<bool, Comet::Error>::success(false);
        }

        const EditorMode requested_mode = *m_requested_mode;
        m_requested_mode.reset();
        if(requested_mode == m_state.mode) {
            return Comet::Result<bool, Comet::Error>::success(false);
        }
        return requested_mode == EditorMode::Play ? enter_play_mode() : exit_play_mode();
    }

    Comet::Result<bool, Comet::Error> EditorSceneSession::enter_play_mode() {
        Comet::Scene* edit_scene = m_get_active_scene();
        if(edit_scene == nullptr) {
            return Comet::Result<bool, Comet::Error>::failure(
                {"Cannot enter Play mode without an active scene"});
        }

        auto runtime_scene = m_serializer.clone(*edit_scene);
        if(!runtime_scene) {
            return Comet::Result<bool, Comet::Error>::failure({runtime_scene.error()});
        }
        auto activated = m_activate_play_scene(std::move(runtime_scene).value());
        if(!activated)
            return Comet::Result<bool, Comet::Error>::failure(activated.error());
        m_edit_scene = std::move(activated).value();
        if(!m_edit_scene) {
            LOG_FATAL("Entering Play mode did not retain the Edit scene");
        }
        if(auto started = m_start_runtime(); !started) {
            auto failed_scene = m_restore_edit_scene(std::move(m_edit_scene));
            return Comet::Result<bool, Comet::Error>::failure(started.error());
        }

        m_state.mode = EditorMode::Play;
        LOG_INFO("Entered Play mode");
        return Comet::Result<bool, Comet::Error>::success(true);
    }

    Comet::Result<bool, Comet::Error> EditorSceneSession::exit_play_mode() {
        if(!m_edit_scene) {
            return Comet::Result<bool, Comet::Error>::failure(
                {"Cannot exit Play mode without the retained Edit scene"});
        }

        std::unique_ptr<Comet::Scene> runtime_scene = m_restore_edit_scene(std::move(m_edit_scene));
        m_state.mode = EditorMode::Edit;
        LOG_INFO("Exited Play mode and restored the Edit scene");
        return Comet::Result<bool, Comet::Error>::success(true);
    }
}
