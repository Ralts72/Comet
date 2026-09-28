#include "project/project_settings.h"

#include "asset/database.h"
#include "core/engine.h"
#include "core/project.h"
#include "diagnostics/logger.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <utility>

namespace CometEditor {
    void ProjectSettings::request_rename() {
        m_name_dialog.request(m_project.name());
    }

    void ProjectSettings::request_input() {
        m_input_panel.request(m_project.input_actions());
    }

    void ProjectSettings::render(const bool editing) {
        m_name_dialog.render();
        if(editing)
            m_input_panel.render();
        else
            m_input_panel.set_visible(false);
    }

    void ProjectSettings::update(Comet::Engine& engine) {
        if(auto actions = m_input_panel.take_request()) {
            const auto saved = m_project.save_input_actions(std::move(*actions));
            m_input_panel.complete(saved);
            if(!saved) {
                LOG_WARN("Cannot save project input actions: {}", saved.error());
            } else if(const auto configured = engine.set_input_actions(m_project.input_actions());
                !configured) {
                LOG_WARN("Project input actions were saved; restart the editor to apply: {}",
                    configured.error().message);
            }
        }
        if(const auto name = m_name_dialog.take_request()) {
            const auto saved = m_project.save_name(*name);
            m_name_dialog.complete(saved);
            if(!saved)
                LOG_WARN("Cannot rename project: {}", saved.error());
            else
                LOG_INFO("Project renamed to '{}'", m_project.name());
        }
    }

    Comet::Result<void> ProjectSettings::set_startup_scene(const std::filesystem::path& path,
        const std::filesystem::path& saved_scene, const Comet::AssetDatabase& assets,
        const Comet::SceneSerializer& serializer) {
        using Result = Comet::Result<void>;
        if(path.empty())
            return Result::failure("Startup scene request has no path");
        const auto* asset = assets.find(path);
        if(path != saved_scene && (!asset || asset->type != Comet::AssetType::Scene))
            return Result::failure(
                "Startup scene is no longer available: " + path.generic_string());
        const auto resolved = m_project.paths().resolve_asset_path(path);
        if(!resolved)
            return Result::failure(resolved.error());
        if(auto loaded = serializer.load(resolved.value().string()); !loaded)
            return Result::failure(
                "Cannot use startup scene '" + path.generic_string() + "': " + loaded.error());
        const auto saved = m_project.save_startup_scene(path);
        if(saved)
            LOG_INFO("Startup scene set to '{}'", path.generic_string());
        return saved;
    }
}
