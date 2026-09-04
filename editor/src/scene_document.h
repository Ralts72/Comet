#pragma once

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

        SceneDocument(const Comet::SceneSerializer& serializer,
            ActiveSceneGetter get_active_scene, ActiveSceneReplacer replace_active_scene);

        [[nodiscard]] bool create_new();
        [[nodiscard]] bool open(const std::string& path);
        [[nodiscard]] bool save(const std::string& path);

        [[nodiscard]] const std::string& get_path() const noexcept { return m_path; }
        [[nodiscard]] const std::string& get_last_error() const noexcept {
            return m_last_error;
        }
        void clear_error() noexcept { m_last_error.clear(); }

    private:
        [[nodiscard]] bool replace_scene(
            std::unique_ptr<Comet::Scene> scene, std::string path);

        const Comet::SceneSerializer& m_serializer;
        ActiveSceneGetter m_get_active_scene;
        ActiveSceneReplacer m_replace_active_scene;
        std::string m_path;
        std::string m_last_error;
    };
}
