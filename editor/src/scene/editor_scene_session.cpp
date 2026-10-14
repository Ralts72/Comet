#include "scene/editor_scene_session.h"

#include "diagnostics/logger.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <utility>

namespace CometEditor {
    EditorSceneSession::EditorSceneSession(EditorState& state,
        const Comet::SceneSerializer& serializer, ActiveSceneGetter get_active_scene,
        ActivatePlayScene activate_play_scene, RestoreEditScene restore_edit_scene,
        StartRuntime start_runtime, TakeSceneRequest take_scene_request,
        Comet::GameSession::LoadScene load_scene)
        : m_state(state), m_game(serializer), m_get_active_scene(std::move(get_active_scene)),
          m_activate_play_scene(std::move(activate_play_scene)),
          m_restore_edit_scene(std::move(restore_edit_scene)),
          m_start_runtime(std::move(start_runtime)),
          m_take_scene_request(std::move(take_scene_request)), m_load_scene(std::move(load_scene)) {
    }

    EditorSceneSession::~EditorSceneSession() = default;

    void EditorSceneSession::set_persistence(Comet::GameSession::Persistence persistence,
        TakeCheckpointRequest take_request, std::function<std::string()> scene_path) {
        m_game.set_persistence(std::move(persistence));
        m_take_checkpoint_request = std::move(take_request);
        m_scene_path = std::move(scene_path);
    }

    void EditorSceneSession::request_mode(const EditorMode mode) {
        if(mode == m_state.mode) {
            m_requested_mode.reset();
            return;
        }
        m_requested_mode = mode;
    }

    Comet::Result<bool, Comet::Error> EditorSceneSession::apply_mode_request(
        const Comet::SceneRuntime::State restart_state) {
        if(m_requested_mode) {
            const EditorMode requested_mode = *m_requested_mode;
            m_requested_mode.reset();
            if(requested_mode != m_state.mode) {
                if(requested_mode == EditorMode::Play)
                    return start_play_mode(Comet::SceneRuntime::State::Running);
                return exit_play_mode();
            }
            return Comet::Result<bool, Comet::Error>::success(false);
        }
        if(m_state.mode == EditorMode::Play && m_take_checkpoint_request)
            if(auto request = m_take_checkpoint_request()) {
                if(auto saved = m_game.save_checkpoint(*request); !saved)
                    LOG_ERROR("Cannot save Play checkpoint: {}", saved.error());
                else
                    LOG_INFO("Saved Play checkpoint '{}'", request->marker);
            }
        if(m_state.mode == EditorMode::Play && m_take_scene_request)
            if(auto request = m_take_scene_request())
                return transition(*request, restart_state);
        return Comet::Result<bool, Comet::Error>::success(false);
    }

    Comet::Result<bool, Comet::Error> EditorSceneSession::start_play_mode(
        const Comet::SceneRuntime::State initial_state) {
        auto* edit_scene = m_get_active_scene();
        if(!edit_scene)
            return Comet::Result<bool, Comet::Error>::failure(
                {"Cannot start Play without its Edit scene"});
        m_game.begin(*edit_scene, m_scene_path ? m_scene_path() : std::string{});
        return transition({}, initial_state);
    }

    Comet::Result<bool, Comet::Error> EditorSceneSession::transition(
        const Comet::RuntimeSession::SceneRequest& request, Comet::SceneRuntime::State state) {
        using Changed = Comet::Result<bool, Comet::Error>;
        auto prepared = m_game.prepare(request, m_load_scene);
        if(!prepared)
            return Changed::failure({prepared.error()});
        auto candidate = std::move(prepared).value();
        auto activated = m_activate_play_scene(std::move(candidate.scene));
        if(!activated)
            return Changed::failure(activated.error());
        if(m_state.mode != EditorMode::Play) {
            m_edit_scene = std::move(activated).value();
            if(!m_edit_scene)
                LOG_FATAL("Entering Play mode did not retain the Edit scene");
        }
        if(auto started = m_start_runtime(state, candidate.values, candidate.checkpoint);
            !started) {
            auto failed_scene = m_restore_edit_scene(std::move(m_edit_scene));
            m_game.end();
            m_state.mode = EditorMode::Edit;
            return Changed::failure(started.error());
        }
        m_game.accept(std::move(candidate));
        m_state.mode = EditorMode::Play;
        LOG_INFO("Play scene started");
        return Changed::success(true);
    }

    Comet::Result<bool, Comet::Error> EditorSceneSession::exit_play_mode() {
        if(!m_edit_scene) {
            return Comet::Result<bool, Comet::Error>::failure(
                {"Cannot exit Play mode without the retained Edit scene"});
        }

        std::unique_ptr<Comet::Scene> runtime_scene = m_restore_edit_scene(std::move(m_edit_scene));
        m_game.end();
        m_state.mode = EditorMode::Edit;
        LOG_INFO("Exited Play mode and restored the Edit scene");
        return Comet::Result<bool, Comet::Error>::success(true);
    }
}
