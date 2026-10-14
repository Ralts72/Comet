#include "gameplay/game_session.h"
#include "scene/scene.h"

namespace Comet {
    GameSession::GameSession(const SceneSerializer& serializer) : m_serializer(serializer) {}
    GameSession::~GameSession() = default;

    void GameSession::begin(const Scene& source, std::string path) {
        end();
        m_source = &source;
        m_path = std::move(path);
    }

    void GameSession::begin(std::unique_ptr<Scene> source, std::string path) {
        end();
        m_owned_source = std::move(source);
        m_source = m_owned_source.get();
        m_path = std::move(path);
    }

    Result<GameSession::Transition> GameSession::prepare(
        const RuntimeSession::SceneRequest& request, const LoadScene& load) const {
        using Prepared = Result<Transition>;
        if(!m_source)
            return Prepared::failure("Game session has not started");
        if(request.kind == RuntimeSession::SceneRequest::Kind::Continue) {
            if(!m_persistence.load)
                return Prepared::failure("Checkpoint loading is unavailable");
            auto saved = m_persistence.load();
            if(!saved)
                return Prepared::failure(saved.error());
            if(saved.value().kind != RuntimeSession::SceneRequest::Kind::Load
                || saved.value().checkpoint.empty())
                return Prepared::failure("Invalid checkpoint scene request");
            return prepare(saved.value(), load);
        }
        Transition transition;
        if(request.kind == RuntimeSession::SceneRequest::Kind::Restart) {
            auto scene = m_serializer.clone(*m_source);
            if(!scene)
                return Prepared::failure(scene.error());
            transition.scene = std::move(scene).value();
            transition.values = m_entry_values;
            transition.path = m_path;
            transition.checkpoint = m_entry_checkpoint;
        } else {
            if(request.kind != RuntimeSession::SceneRequest::Kind::Load || !load
                || request.path.empty() || !RuntimeSession::valid_values(request.values)
                || (!request.checkpoint.empty() && !valid_parameter_name(request.checkpoint)))
                return Prepared::failure("Invalid scene transition request");
            auto scene = load(request.path);
            if(!scene)
                return Prepared::failure(scene.error());
            if(!scene.value())
                return Prepared::failure("Scene loader returned an empty scene");
            auto source = m_serializer.clone(*scene.value());
            if(!source)
                return Prepared::failure(source.error());
            transition.scene = std::move(scene).value();
            transition.source = std::move(source).value();
            transition.values = request.values;
            transition.path = request.path;
            transition.checkpoint = request.checkpoint;
        }
        return Prepared::success(std::move(transition));
    }

    void GameSession::accept(Transition transition) {
        if(!transition.source)
            return;
        m_owned_source = std::move(transition.source);
        m_source = m_owned_source.get();
        m_entry_values = std::move(transition.values);
        m_path = std::move(transition.path);
        m_entry_checkpoint = std::move(transition.checkpoint);
    }

    Result<void> GameSession::save_checkpoint(
        const RuntimeSession::CheckpointRequest& request) const {
        if(!m_source || m_path.empty() || !m_persistence.save)
            return Result<void>::failure("Checkpoint saving requires a saved project scene");
        return m_persistence.save(m_path, request);
    }

    void GameSession::end() {
        m_source = nullptr;
        m_owned_source.reset();
        m_entry_values.clear();
        m_path.clear();
        m_entry_checkpoint.clear();
    }
}
