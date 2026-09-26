#pragma once

#include "core/project_paths.h"
#include "input/input_actions.h"
#include "common/result.h"

#include <cstdint>
#include <string>

namespace Comet {
    class COMET_API Project final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 1;

        [[nodiscard]] static Result<Project> load(const std::filesystem::path& path);
        [[nodiscard]] Result<void> save_name(std::string name);
        [[nodiscard]] Result<void> save_startup_scene(const std::filesystem::path& path);

        [[nodiscard]] const ProjectPaths& paths() const { return m_paths; }
        [[nodiscard]] const std::string& name() const { return m_name; }
        [[nodiscard]] const std::filesystem::path& startup_scene() const { return m_startup_scene; }
        [[nodiscard]] const InputActions& input_actions() const { return m_input_actions; }

    private:
        explicit Project(ProjectPaths paths);
        [[nodiscard]] Result<std::string> serialize(
            const std::string& name, const std::filesystem::path& startup_scene) const;
        [[nodiscard]] Result<void> save_settings(
            std::string name, const std::filesystem::path& startup_scene);

        ProjectPaths m_paths;
        std::string m_name;
        std::filesystem::path m_startup_scene;
        InputActions m_input_actions;
        std::string m_source_contents;
    };
}
