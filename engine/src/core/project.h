#pragma once

#include "core/project_paths.h"

#include <cstdint>
#include <string>

namespace Comet {
    class COMET_API Project final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 1;

        [[nodiscard]] static Project load(const std::filesystem::path& path);

        [[nodiscard]] const ProjectPaths& paths() const { return m_paths; }
        [[nodiscard]] const std::string& name() const { return m_name; }
        [[nodiscard]] const std::filesystem::path& startup_scene() const {
            return m_startup_scene;
        }

    private:
        explicit Project(ProjectPaths paths);

        ProjectPaths m_paths;
        std::string m_name;
        std::filesystem::path m_startup_scene;
    };
}
