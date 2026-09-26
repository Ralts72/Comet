#pragma once

#include "common/result.h"
#include "core/project_paths.h"

#include <filesystem>
#include <optional>

namespace CometEditor {
    class ProjectSession {
    public:
        explicit ProjectSession(Comet::ProjectPaths paths);

        [[nodiscard]] Comet::Result<void> load();
        [[nodiscard]] Comet::Result<void> record_scene(const std::filesystem::path& path);
        [[nodiscard]] const std::optional<std::filesystem::path>& last_scene() const noexcept {
            return m_last_scene;
        }

    private:
        [[nodiscard]] Comet::Result<std::filesystem::path> validate_scene(
            const std::filesystem::path& path) const;

        Comet::ProjectPaths m_paths;
        std::filesystem::path m_file;
        std::optional<std::filesystem::path> m_last_scene;
    };
}
