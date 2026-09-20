#include "core/project_paths.h"

#include "diagnostics/logger.h"

#include <utility>
#include <system_error>

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

    Result<std::filesystem::path> ProjectPaths::resolve_asset_path(
        const std::filesystem::path& path) const {
        if(path.empty())
            return Result<std::filesystem::path>::failure("Asset path cannot be empty");
        std::error_code error;
        const auto absolute = std::filesystem::absolute(assets(), error);
        if(error)
            return Result<std::filesystem::path>::failure(
                "Cannot resolve assets directory: " + error.message());
        const auto directory = std::filesystem::weakly_canonical(absolute, error);
        if(error)
            return Result<std::filesystem::path>::failure(
                "Cannot resolve assets directory: " + error.message());
        const auto resolved =
            std::filesystem::weakly_canonical(path.is_absolute() ? path : directory / path, error);
        if(error)
            return Result<std::filesystem::path>::failure(
                "Cannot resolve asset path '" + path.string() + "': " + error.message());
        const auto relative = resolved.lexically_relative(directory);
        if(relative.empty() || relative == "." || relative.is_absolute()
            || *relative.begin() == "..")
            return Result<std::filesystem::path>::failure(
                "Path is outside project assets: " + path.string());
        return Result<std::filesystem::path>::success(resolved);
    }

    std::filesystem::path ProjectPaths::local_data() const {
        return m_root / ".comet";
    }

    std::filesystem::path ProjectPaths::cache() const {
        return local_data() / "cache";
    }

    std::filesystem::path ProjectPaths::logs() const {
        return local_data() / "logs";
    }

    std::filesystem::path ProjectPaths::editor_state() const {
        return local_data() / "editor";
    }

}
