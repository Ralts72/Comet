#pragma once

#include "common/export.h"
#include "common/result.h"
#include "asset/data/mesh_data.h"
#include "asset/import/asset_task_types.h"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace Comet {
    struct COMET_API MeshImportData {
        MeshData data;
        std::vector<std::filesystem::path> source_dependencies;
    };

    class COMET_API MeshImporter final {
    public:
        static constexpr std::uint32_t VERSION = 1;
        [[nodiscard]] static Result<std::size_t> working_bytes(
            const std::filesystem::path& source_path, const AssetImportLimits& limits = {});
        [[nodiscard]] Result<MeshData> import(const std::filesystem::path& source_path,
            std::size_t memory_budget = AssetImportLimits{}.mesh_working_bytes,
            const AssetImportLimits& limits = {}) const;
        [[nodiscard]] Result<MeshImportData> import_with_dependencies(
            const std::filesystem::path& source_path,
            std::size_t memory_budget = AssetImportLimits{}.mesh_working_bytes,
            const AssetImportLimits& limits = {}) const;
    };
}
