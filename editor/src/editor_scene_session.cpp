#include "editor_scene_session.h"

#include "diagnostics/logger.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "runtime/scene_runtime.h"

#include <utility>

namespace CometEditor {
    EditorSceneSession::EditorSceneSession(EditorState& state,
        Comet::SceneRuntime& runtime, const Comet::SceneSerializer& serializer,
        ActiveSceneGetter get_active_scene, ActiveSceneReplacer replace_active_scene)
        : m_state(state), m_runtime(runtime), m_serializer(serializer),
          m_get_active_scene(std::move(get_active_scene)),
          m_replace_active_scene(std::move(replace_active_scene)) {}

    EditorSceneSession::~EditorSceneSession() {
        m_runtime.stop();
    }

    void EditorSceneSession::request_mode(const EditorMode mode) {
        if(mode == m_state.mode) {
            m_requested_mode.reset();
            return;
        }
        m_requested_mode = mode;
    }

    bool EditorSceneSession::apply_mode_request() {
        if(!m_requested_mode) {
            return false;
        }

        const EditorMode requested_mode = *m_requested_mode;
        m_requested_mode.reset();
        if(requested_mode == m_state.mode) {
            return false;
        }
        return requested_mode == EditorMode::Play ? enter_play_mode() : exit_play_mode();
    }

    bool EditorSceneSession::enter_play_mode() {
        Comet::Scene* edit_scene = m_get_active_scene();
        if(edit_scene == nullptr) {
            LOG_ERROR("Cannot enter Play mode without an active scene");
            return false;
        }

        std::unique_ptr<Comet::Scene> runtime_scene = m_serializer.clone(*edit_scene);
        m_edit_scene = m_replace_active_scene(std::move(runtime_scene));
        if(!m_edit_scene) {
            LOG_FATAL("Entering Play mode did not retain the Edit scene");
        }

        m_state.mode = EditorMode::Play;
        // 启动失败时保留 Play 副本与 Edit 原件，允许 Stop 恢复，不悬空面板。
        m_runtime.start(*m_get_active_scene());
        LOG_INFO("Entered Play mode");
        return true;
    }

    bool EditorSceneSession::exit_play_mode() {
        if(!m_edit_scene) {
            LOG_ERROR("Cannot exit Play mode without the retained Edit scene");
            return false;
        }

        m_runtime.stop();
        std::unique_ptr<Comet::Scene> runtime_scene =
            m_replace_active_scene(std::move(m_edit_scene));
        m_state.mode = EditorMode::Edit;
        LOG_INFO("Exited Play mode and restored the Edit scene");
        return true;
    }
}
