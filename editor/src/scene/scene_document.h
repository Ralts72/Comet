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
        using ActiveSceneReplacer =
            std::function<std::unique_ptr<Comet::Scene>(std::unique_ptr<Comet::Scene>)>;
        using PrepareCandidate = std::function<Comet::Result<void, Comet::Error>(Comet::Scene&)>;

        SceneDocument(const Comet::SceneSerializer& serializer, Comet::ProjectPaths paths,
            const CommandHistory& history, ActiveSceneGetter get_active_scene,
            ActiveSceneReplacer replace_active_scene, PrepareCandidate prepare_candidate = {});

        [[nodiscard]] Comet::Result<void, Comet::Error> create_new();
        [[nodiscard]] Comet::Result<void, Comet::Error> open(const std::string& path);
        [[nodiscard]] Comet::Result<void, Comet::Error> save(const std::string& path);

        [[nodiscard]] const std::string& get_path() const noexcept { return m_path; }
        [[nodiscard]] const std::string& get_last_error() const noexcept { return m_last_error; }
        void clear_error() noexcept { m_last_error.clear(); }
        [[nodiscard]] bool is_modified() const { return m_saved_state != m_history.state_id(); }
        void request(Request request);
        void decide(Decision decision);
        [[nodiscard]] bool has_pending_request() const { return m_pending_request.has_value(); }
        [[nodiscard]] bool needs_confirmation() const;
        [[nodiscard]] std::optional<Request> take_ready_request();

    private:
        [[nodiscard]] Comet::Result<void, Comet::Error> replace_scene(
            std::unique_ptr<Comet::Scene> scene, std::string path);

        const Comet::SceneSerializer& m_serializer;
        const CommandHistory& m_history;
        std::uint64_t m_saved_state;
        Comet::ProjectPaths m_paths;
        ActiveSceneGetter m_get_active_scene;
        ActiveSceneReplacer m_replace_active_scene;
        PrepareCandidate m_prepare_candidate;
        std::string m_path;
        std::string m_last_error;
        enum class PendingState { Confirm, Saving, Discard };
        struct PendingRequest {
            Request action;
            PendingState state = PendingState::Confirm;
        };
        std::optional<PendingRequest> m_pending_request;
    };
}
