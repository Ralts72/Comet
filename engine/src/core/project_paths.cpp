#include "core/project_paths.h"

#include "diagnostics/logger.h"

#include <utility>
#include <stdexcept>

namespace Comet {
    ProjectPaths::ProjectPaths(std::filesystem::path root)
        : m_root(std::move(root).lexically_normal()) {
        if(m_root.empty()) {
            LOG_FATAL("Project root path cannot be empty");
        }
    }

    const std::filesystem::path& ProjectPaths::root() const noexcept {
        return m_root;
    }

    std::filesystem::path ProjectPaths::assets() const {
        return m_root / "assets";
    }

    std::filesystem::path ProjectPaths::resolve_asset_path(
        const std::filesystem::path& path) const {
        if(path.empty())
            throw std::runtime_error("Asset path cannot be empty");
        const auto directory =
            std::filesystem::weakly_canonical(std::filesystem::absolute(assets()));
        const auto resolved = std::filesystem::weakly_canonical(
            path.is_absolute() ? path : directory / path);
        const auto relative = resolved.lexically_relative(directory);
        if(relative.empty() || relative == "." || relative.is_absolute()
            || *relative.begin() == "..")
            throw std::runtime_error("Path is outside project assets: " + path.string());
        return resolved;
    }

    std::filesystem::path ProjectPaths::local_data() const {
        return m_root / ".comet";
    }

    std::filesystem::path ProjectPaths::cache() const {
        return local_data() / "cache";
    }

    std::filesystem::path ProjectPaths::editor_state() const {
        return local_data() / "editor";
    }

}
