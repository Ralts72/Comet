#include "scene/scene_document.h"

#include "diagnostics/logger.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <filesystem>
#include <utility>

namespace CometEditor {
    SceneDocument::SceneDocument(const Comet::SceneSerializer& serializer,
        Comet::ProjectPaths paths, ActiveSceneGetter get_active_scene,
        ActiveSceneReplacer replace_active_scene, PrepareCandidate prepare_candidate)
        : m_serializer(serializer), m_paths(std::move(paths)),
          m_get_active_scene(std::move(get_active_scene)),
          m_replace_active_scene(std::move(replace_active_scene)),
          m_prepare_candidate(std::move(prepare_candidate)) {}

    Comet::Result<void, Comet::Error> SceneDocument::create_new() {
        if(auto replaced = replace_scene(std::make_unique<Comet::Scene>(), {}); !replaced) {
            return replaced;
        }
        LOG_INFO("Created new scene");
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void, Comet::Error> SceneDocument::open(const std::string& path) {
        if(path.empty()) {
            m_last_error = "Scene path cannot be empty";
            return Comet::Result<void, Comet::Error>::failure({m_last_error});
        }

        const auto resolved = m_paths.resolve_asset_path(path);
        if(!resolved || resolved.value().extension() != ".scene") {
            m_last_error = !resolved ? resolved.error() : "Scene file must have a .scene extension";
            LOG_ERROR("Failed to open scene '{}': {}", path, m_last_error);
            return Comet::Result<void, Comet::Error>::failure({m_last_error});
        }
        auto scene = m_serializer.load(resolved.value().string());
        if(!scene) {
            m_last_error = scene.error();
            LOG_ERROR("Failed to open scene '{}': {}", path, m_last_error);
            return Comet::Result<void, Comet::Error>::failure({m_last_error});
        }
        if(auto replaced = replace_scene(std::move(scene).value(), resolved.value().string());
            !replaced)
            return replaced;
        LOG_INFO("Opened scene '{}'", path);
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void, Comet::Error> SceneDocument::save(const std::string& path) {
        const Comet::Scene* scene = m_get_active_scene();
        if(scene == nullptr) {
            m_last_error = "No active scene to save";
            return Comet::Result<void, Comet::Error>::failure({m_last_error});
        }
        if(path.empty()) {
            m_last_error = "Scene path cannot be empty";
            return Comet::Result<void, Comet::Error>::failure({m_last_error});
        }

        const auto resolved = m_paths.resolve_asset_path(path);
        if(!resolved || resolved.value().extension() != ".scene") {
            m_last_error = !resolved ? resolved.error() : "Scene file must have a .scene extension";
            LOG_ERROR("Failed to save scene '{}': {}", path, m_last_error);
            return Comet::Result<void, Comet::Error>::failure({m_last_error});
        }
        const auto saved = m_serializer.save(*scene, resolved.value().string());
        if(!saved) {
            m_last_error = saved.error();
            LOG_ERROR("Failed to save scene '{}': {}", path, m_last_error);
            return Comet::Result<void, Comet::Error>::failure({m_last_error});
        }
        m_path = resolved.value().string();
        m_last_error.clear();
        LOG_INFO("Saved scene '{}'", path);
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void, Comet::Error> SceneDocument::replace_scene(
        std::unique_ptr<Comet::Scene> scene, std::string path) {
        if(!scene) {
            m_last_error = "Cannot activate an empty scene";
            LOG_ERROR("{}", m_last_error);
            return Comet::Result<void, Comet::Error>::failure({m_last_error});
        }

        if(m_prepare_candidate) {
            if(auto prepared = m_prepare_candidate(*scene); !prepared) {
                m_last_error = prepared.error().message;
                LOG_ERROR("Cannot prepare scene: {}", m_last_error);
                return Comet::Result<void, Comet::Error>::failure(prepared.error());
            }
        }
        static_cast<void>(m_replace_active_scene(std::move(scene)));
        m_path = std::move(path);
        m_last_error.clear();
        return Comet::Result<void, Comet::Error>::success();
    }
}
