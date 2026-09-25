#pragma once

#include "asset/database.h"
#include "asset/data/material_data.h"
#include "asset/import/asset_task_types.h"
#include "common/result.h"
#include "core/project_paths.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace CometEditor::AssetSourceOperations {
    using TrashMover = std::function<Comet::Result<void>(const std::filesystem::path&)>;

    class PreparedFileImport final {
    public:
        PreparedFileImport(PreparedFileImport&&) noexcept;
        PreparedFileImport& operator=(PreparedFileImport&&) noexcept;
        ~PreparedFileImport();

        PreparedFileImport(const PreparedFileImport&) = delete;
        PreparedFileImport& operator=(const PreparedFileImport&) = delete;

        [[nodiscard]] static Comet::Result<PreparedFileImport> prepare(Comet::ProjectPaths paths,
            std::vector<std::filesystem::path> sources, std::filesystem::path directory,
            Comet::AssetImportLimits limits);
        [[nodiscard]] Comet::AssetScanReport publish(Comet::AssetDatabase& database) &&;

    private:
        struct State;
        explicit PreparedFileImport(std::unique_ptr<State> state);
        std::unique_ptr<State> m_state;
    };

    [[nodiscard]] Comet::AssetScanReport create_material(Comet::AssetDatabase& database,
        const std::filesystem::path& destination, const Comet::MaterialData& data);

    [[nodiscard]] Comet::AssetScanReport create_script(
        Comet::AssetDatabase& database, const std::filesystem::path& destination);

    [[nodiscard]] Comet::AssetScanReport move(Comet::AssetDatabase& database,
        Comet::AssetHandle handle, const std::filesystem::path& destination);

    [[nodiscard]] Comet::AssetScanReport remove_asset(
        Comet::AssetDatabase& database, Comet::AssetHandle handle, const TrashMover& move_to_trash);

    [[nodiscard]] Comet::AssetScanReport import_files(Comet::AssetDatabase& database,
        std::span<const std::filesystem::path> sources, const std::filesystem::path& directory,
        Comet::AssetImportLimits limits);
}
