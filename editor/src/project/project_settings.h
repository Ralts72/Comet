#pragma once

#include "common/result.h"
#include "project/input_settings_panel.h"
#include "project/project_name_dialog.h"

#include <filesystem>

namespace Comet {
    class AssetDatabase;
    class Engine;
    class Project;
    class SceneSerializer;
}

namespace CometEditor {
    class ProjectSettings final {
    public:
        explicit ProjectSettings(Comet::Project& project) : m_project(project) {}

        void request_rename();
        void request_input();
        void render(bool editing);
        void update(Comet::Engine& engine);
        [[nodiscard]] Comet::Result<void> set_startup_scene(const std::filesystem::path& path,
            const std::filesystem::path& saved_scene, const Comet::AssetDatabase& assets,
            const Comet::SceneSerializer& serializer);

    private:
        Comet::Project& m_project;
        ProjectNameDialog m_name_dialog;
        InputSettingsPanel m_input_panel;
    };
}
