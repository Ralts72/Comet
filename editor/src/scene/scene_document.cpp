#include "scene/scene_document.h"

#include "diagnostics/logger.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace CometEditor {
    SceneDocument::SceneDocument(const Comet::SceneSerializer& serializer,
        Comet::ProjectPaths paths, const CommandHistory& history,
        ActiveSceneGetter get_active_scene, ActivateScene activate_scene)
        : m_serializer(serializer), m_history(history), m_saved_state(history.state_id()),
          m_paths(std::move(paths)), m_get_active_scene(std::move(get_active_scene)),
          m_activate_scene(std::move(activate_scene)) {}

    Comet::Result<void, Comet::Error> SceneDocument::create_new() {
        if(auto activated = activate_scene(std::make_unique<Comet::Scene>(), {}); !activated)
            return activated;
        LOG_INFO("Created new scene");
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void, Comet::Error> SceneDocument::open(const std::string& path) {
        if(path.empty())
            return Comet::Result<void, Comet::Error>::failure({"Scene path cannot be empty"});

        const auto resolved = m_paths.resolve_asset_path(path);
        if(!resolved || resolved.value().extension() != ".scene") {
            const auto message =
                !resolved ? resolved.error() : "Scene file must have a .scene extension";
            LOG_ERROR("Failed to open scene '{}': {}", path, message);
            return Comet::Result<void, Comet::Error>::failure({message});
        }
        auto scene = m_serializer.load(resolved.value().string());
        if(!scene) {
            LOG_ERROR("Failed to open scene '{}': {}", path, scene.error());
            return Comet::Result<void, Comet::Error>::failure({scene.error()});
        }
        if(auto activated = activate_scene(std::move(scene).value(), resolved.value().string());
            !activated)
            return activated;
        LOG_INFO("Opened scene '{}'", path);
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void, Comet::Error> SceneDocument::save(const std::string& path) {
        const Comet::Scene* scene = m_get_active_scene();
        if(scene == nullptr)
            return Comet::Result<void, Comet::Error>::failure({"No active scene to save"});
        if(path.empty())
            return Comet::Result<void, Comet::Error>::failure({"Scene path cannot be empty"});

        const auto resolved = m_paths.resolve_asset_path(path);
        if(!resolved || resolved.value().extension() != ".scene") {
            const auto message =
                !resolved ? resolved.error() : "Scene file must have a .scene extension";
            LOG_ERROR("Failed to save scene '{}': {}", path, message);
            return Comet::Result<void, Comet::Error>::failure({message});
        }
        std::error_code error;
        const auto asset_relative_path =
            std::filesystem::relative(resolved.value(), m_paths.assets(), error);
        if(error) {
            const auto message = "Cannot resolve scene path relative to assets: " + error.message();
            LOG_ERROR("Failed to save scene '{}': {}", path, message);
            return Comet::Result<void, Comet::Error>::failure({message});
        }
        const auto saved = m_serializer.save(*scene, resolved.value().string());
        if(!saved) {
            LOG_ERROR("Failed to save scene '{}': {}", path, saved.error());
            return Comet::Result<void, Comet::Error>::failure({saved.error()});
        }
        m_path = resolved.value().string();
        m_asset_relative_path = asset_relative_path;
        m_saved_state = m_history.state_id();
        if(m_pending_request)
            m_pending_request->state = PendingState::Confirm;
        LOG_INFO("Saved scene '{}'", path);
        return Comet::Result<void, Comet::Error>::success();
    }

    void SceneDocument::relocate_asset(
        const std::filesystem::path& source, const std::filesystem::path& destination) {
        if(m_asset_relative_path != source)
            return;
        m_asset_relative_path = destination;
        m_path = (m_paths.assets() / destination).string();
    }

    void SceneDocument::request(Request request) {
        if(m_pending_request)
            return;
        m_pending_request = PendingRequest{std::move(request)};
    }

    void SceneDocument::decide(const Decision decision) {
        if(decision == Decision::Cancel) {
            m_pending_request.reset();
        } else if(m_pending_request) {
            m_pending_request->state =
                decision == Decision::Save ? PendingState::Saving : PendingState::Discard;
        }
    }

    bool SceneDocument::needs_confirmation() const {
        return m_pending_request && m_pending_request->state == PendingState::Confirm
               && is_modified();
    }

    std::optional<SceneDocument::Request> SceneDocument::take_ready_request() {
        if(!m_pending_request || m_pending_request->state == PendingState::Saving
            || needs_confirmation())
            return std::nullopt;
        auto pending = std::exchange(m_pending_request, std::nullopt);
        return std::move(pending->action);
    }

    Comet::Result<void, Comet::Error> SceneDocument::activate_scene(
        std::unique_ptr<Comet::Scene> scene, std::string path) {
        if(!scene) {
            LOG_ERROR("Cannot activate an empty scene");
            return Comet::Result<void, Comet::Error>::failure({"Cannot activate an empty scene"});
        }

        std::filesystem::path asset_relative_path;
        if(!path.empty()) {
            std::error_code error;
            asset_relative_path = std::filesystem::relative(path, m_paths.assets(), error);
            if(error) {
                const auto message =
                    "Cannot resolve scene path relative to assets: " + error.message();
                LOG_ERROR("Cannot activate scene: {}", message);
                return Comet::Result<void, Comet::Error>::failure({message});
            }
        }

        if(auto activated = m_activate_scene(std::move(scene)); !activated) {
            LOG_ERROR("Cannot activate scene: {}", activated.error().message);
            return activated;
        }
        m_path = std::move(path);
        m_asset_relative_path = std::move(asset_relative_path);
        m_saved_state = m_history.state_id();
        return Comet::Result<void, Comet::Error>::success();
    }
}
