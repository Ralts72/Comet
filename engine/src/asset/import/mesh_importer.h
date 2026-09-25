#pragma once

#include "common/export.h"
#include "common/result.h"
#include "asset/data/mesh_data.h"

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
        static constexpr std::size_t MAX_WORKING_BYTES = 1024ull * 1024 * 1024;

        [[nodiscard]] static Result<std::size_t> working_bytes(
            const std::filesystem::path& source_path);
        [[nodiscard]] Result<MeshData> import(const std::filesystem::path& source_path,
            std::size_t memory_budget = MAX_WORKING_BYTES) const;
        [[nodiscard]] Result<MeshImportData> import_with_dependencies(
            const std::filesystem::path& source_path,
            std::size_t memory_budget = MAX_WORKING_BYTES) const;
    };
}
