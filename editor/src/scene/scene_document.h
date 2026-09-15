#pragma once

#include "core/project_paths.h"
#include "common/result.h"
#include "common/error.h"

#include <functional>
#include <memory>
#include <string>

namespace Comet {
    class Scene;
    class SceneSerializer;
}

namespace CometEditor {
    class SceneDocument final {
    public:
        using ActiveSceneGetter = std::function<Comet::Scene*()>;
        using ActiveSceneReplacer =
            std::function<std::unique_ptr<Comet::Scene>(std::unique_ptr<Comet::Scene>)>;
        using PrepareCandidate = std::function<Comet::Result<void, Comet::Error>(Comet::Scene&)>;

        SceneDocument(const Comet::SceneSerializer& serializer, Comet::ProjectPaths paths,
            ActiveSceneGetter get_active_scene, ActiveSceneReplacer replace_active_scene,
            PrepareCandidate prepare_candidate = {});

        [[nodiscard]] Comet::Result<void, Comet::Error> create_new();
        [[nodiscard]] Comet::Result<void, Comet::Error> open(const std::string& path);
        [[nodiscard]] Comet::Result<void, Comet::Error> save(const std::string& path);

        [[nodiscard]] const std::string& get_path() const noexcept { return m_path; }
        [[nodiscard]] const std::string& get_last_error() const noexcept { return m_last_error; }
        void clear_error() noexcept { m_last_error.clear(); }

    private:
        [[nodiscard]] Comet::Result<void, Comet::Error> replace_scene(
            std::unique_ptr<Comet::Scene> scene, std::string path);

        const Comet::SceneSerializer& m_serializer;
        Comet::ProjectPaths m_paths;
        ActiveSceneGetter m_get_active_scene;
        ActiveSceneReplacer m_replace_active_scene;
        PrepareCandidate m_prepare_candidate;
        std::string m_path;
        std::string m_last_error;
    };
}
