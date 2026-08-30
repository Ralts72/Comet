#include "core/project_paths.h"

#include "diagnostics/logger.h"

#include <utility>

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

    std::filesystem::path ProjectPaths::local_data() const {
        return m_root / ".comet";
    }

    std::filesystem::path ProjectPaths::cache() const {
        return local_data() / "cache";
    }

    std::filesystem::path ProjectPaths::editor_state() const {
        return local_data() / "editor";
    }

    std::filesystem::path ProjectPaths::settings() const {
        return m_root / "ProjectSettings";
    }
}
