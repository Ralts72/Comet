#pragma once

#include "common/export.h"
#include "asset/result.h"
#include "render/resource/mesh_data.h"

#include <cstdint>
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

        [[nodiscard]] AssetResult<MeshData> import(
            const std::filesystem::path& source_path) const;
        [[nodiscard]] AssetResult<MeshImportData> import_with_dependencies(
            const std::filesystem::path& source_path) const;
    };
}
