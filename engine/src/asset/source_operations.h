#pragma once

#include "asset/database.h"
#include "asset/data/material_data.h"
#include "common/export.h"
#include "common/result.h"
#include "core/project_paths.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace Comet::AssetSourceOperations {
    class COMET_API PreparedFileImport final {
    public:
        PreparedFileImport(PreparedFileImport&&) noexcept;
        PreparedFileImport& operator=(PreparedFileImport&&) noexcept;
        ~PreparedFileImport();

        PreparedFileImport(const PreparedFileImport&) = delete;
        PreparedFileImport& operator=(const PreparedFileImport&) = delete;

        [[nodiscard]] static Result<PreparedFileImport> prepare(ProjectPaths paths,
            std::vector<std::filesystem::path> sources, std::filesystem::path directory,
            std::uintmax_t source_byte_budget);
        [[nodiscard]] AssetScanReport publish(AssetDatabase& database) &&;

    private:
        struct State;
        explicit PreparedFileImport(std::unique_ptr<State> state);
        std::unique_ptr<State> m_state;
    };

    [[nodiscard]] COMET_API AssetScanReport create_material(AssetDatabase& database,
        const ProjectPaths& paths, const std::filesystem::path& destination,
        const MaterialData& data);

    [[nodiscard]] COMET_API AssetScanReport create_script(AssetDatabase& database,
        const ProjectPaths& paths, const std::filesystem::path& destination);

    [[nodiscard]] COMET_API AssetScanReport move(AssetDatabase& database, const ProjectPaths& paths,
        AssetHandle handle, const std::filesystem::path& destination);

    [[nodiscard]] COMET_API AssetScanReport remove_asset(
        AssetDatabase& database, const ProjectPaths& paths, AssetHandle handle);

    [[nodiscard]] COMET_API AssetScanReport import_files(AssetDatabase& database,
        const ProjectPaths& paths, std::span<const std::filesystem::path> sources,
        const std::filesystem::path& directory, std::uintmax_t source_byte_budget);
}
