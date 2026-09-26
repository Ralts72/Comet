#pragma once

#include "common/result.h"

#include <filesystem>
#include <utility>
#include <vector>

namespace CometEditor {
    class RecentProjects {
    public:
        [[nodiscard]] static Comet::Result<std::filesystem::path> default_storage_path();
        [[nodiscard]] static Comet::Result<RecentProjects> load(std::filesystem::path file);

        [[nodiscard]] const std::vector<std::filesystem::path>& entries() const noexcept {
            return m_entries;
        }
        [[nodiscard]] Comet::Result<void> record(const std::filesystem::path& root);

    private:
        explicit RecentProjects(std::filesystem::path file) : m_file(std::move(file)) {}

        std::filesystem::path m_file;
        std::vector<std::filesystem::path> m_entries;
    };
}
