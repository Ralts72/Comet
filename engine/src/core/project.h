#pragma once

#include "core/project_paths.h"
#include "config/display_settings.h"
#include "input/input_actions.h"
#include "common/result.h"
#include "common/uuid.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Comet {
    class COMET_API Project final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 2;

        struct UiEntry {
            std::filesystem::path document;
            std::filesystem::path controller;
            bool operator==(const UiEntry&) const = default;
        };

        [[nodiscard]] static Result<Project> load(const std::filesystem::path& path);
        [[nodiscard]] Result<void> save_name(std::string name);
        [[nodiscard]] Result<void> save_startup_scene(const std::filesystem::path& path);
        [[nodiscard]] Result<void> save_input_actions(InputActions actions);
        [[nodiscard]] Result<void> save_display_settings(DisplaySettings settings);

        [[nodiscard]] const ProjectPaths& paths() const { return m_paths; }
        [[nodiscard]] Uuid id() const { return m_id; }
        [[nodiscard]] const std::string& name() const { return m_name; }
        [[nodiscard]] const std::filesystem::path& startup_scene() const { return m_startup_scene; }
        [[nodiscard]] const InputActions& input_actions() const { return m_input_actions; }
        [[nodiscard]] const DisplaySettings& display_settings() const { return m_display_settings; }
        [[nodiscard]] const std::optional<UiEntry>& ui() const { return m_ui; }

    private:
        explicit Project(ProjectPaths paths);
        [[nodiscard]] Result<std::string> serialize(const std::string& name,
            const std::filesystem::path& startup_scene, const InputActions& input_actions,
            const DisplaySettings& display_settings) const;
        [[nodiscard]] Result<void> save_settings(std::string name,
            const std::filesystem::path& startup_scene, InputActions input_actions);

        ProjectPaths m_paths;
        Uuid m_id;
        std::string m_name;
        std::filesystem::path m_startup_scene;
        InputActions m_input_actions;
        DisplaySettings m_display_settings;
        std::optional<UiEntry> m_ui;
    };
}
