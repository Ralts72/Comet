#pragma once

#include "common/result.h"
#include "project/display_settings_panel.h"
#include "project/quality_settings_panel.h"
#include "project/input_settings_panel.h"
#include "project/project_name_dialog.h"

#include <filesystem>

namespace Comet {
    class AssetDatabase;
    class Project;
    class SceneSerializer;
}

namespace CometEditor {
    class ProjectSettings final {
    public:
        struct Update {
            bool input_changed = false;
            bool display_changed = false;
            bool quality_changed = false;
        };
        explicit ProjectSettings(Comet::Project& project) : m_project(project) {}

        void request_rename();
        void request_input();
        void request_display();
        void request_quality();
        void render(bool editing, const Comet::Input::Frame& input);
        [[nodiscard]] Update update();
        [[nodiscard]] Comet::Result<void> set_startup_scene(const std::filesystem::path& path,
            const std::filesystem::path& saved_scene, const Comet::AssetDatabase& assets,
            const Comet::SceneSerializer& serializer);

    private:
        Comet::Project& m_project;
        ProjectNameDialog m_name_dialog;
        InputSettingsPanel m_input_panel;
        DisplaySettingsPanel m_display_panel;
        QualitySettingsPanel m_quality_panel;
    };
}
