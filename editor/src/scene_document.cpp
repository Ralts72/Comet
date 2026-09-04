#include "scene_document.h"

#include "diagnostics/logger.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <exception>
#include <utility>

namespace CometEditor {
    SceneDocument::SceneDocument(const Comet::SceneSerializer& serializer,
        ActiveSceneGetter get_active_scene, ActiveSceneReplacer replace_active_scene)
        : m_serializer(serializer), m_get_active_scene(std::move(get_active_scene)),
          m_replace_active_scene(std::move(replace_active_scene)) {}

    bool SceneDocument::create_new() {
        if(!replace_scene(std::make_unique<Comet::Scene>(), {})) {
            return false;
        }
        LOG_INFO("Created new scene");
        return true;
    }

    bool SceneDocument::open(const std::string& path) {
        if(path.empty()) {
            m_last_error = "Scene path cannot be empty";
            return false;
        }

        try {
            std::unique_ptr<Comet::Scene> scene = m_serializer.load(path);
            if(!replace_scene(std::move(scene), path)) {
                return false;
            }
            LOG_INFO("Opened scene '{}'", path);
            return true;
        } catch(const std::exception& error) {
            m_last_error = error.what();
            LOG_ERROR("Failed to open scene '{}': {}", path, error.what());
            return false;
        }
    }

    bool SceneDocument::save(const std::string& path) {
        const Comet::Scene* scene = m_get_active_scene();
        if(scene == nullptr) {
            m_last_error = "No active scene to save";
            return false;
        }
        if(path.empty()) {
            m_last_error = "Scene path cannot be empty";
            return false;
        }

        try {
            m_serializer.save(*scene, path);
            m_path = path;
            m_last_error.clear();
            LOG_INFO("Saved scene '{}'", path);
            return true;
        } catch(const std::exception& error) {
            m_last_error = error.what();
            LOG_ERROR("Failed to save scene '{}': {}", path, error.what());
            return false;
        }
    }

    bool SceneDocument::replace_scene(
        std::unique_ptr<Comet::Scene> scene, std::string path) {
        if(!scene) {
            m_last_error = "Cannot activate an empty scene";
            LOG_ERROR("{}", m_last_error);
            return false;
        }

        static_cast<void>(m_replace_active_scene(std::move(scene)));
        m_path = std::move(path);
        m_last_error.clear();
        return true;
    }
}
