#pragma once

#include "common/export.h"
#include "render/resource/mesh_data.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Comet {
    struct COMET_API MeshImportResult {
        MeshData data;
        std::vector<std::filesystem::path> source_dependencies;
    };

    class COMET_API MeshImporter final {
    public:
        static constexpr std::uint32_t OUTPUT_VERSION = 1;

        [[nodiscard]] MeshData import(
            const std::filesystem::path& source_path) const;
        [[nodiscard]] MeshImportResult import_with_dependencies(
            const std::filesystem::path& source_path) const;
    };
}
