#pragma once

#include "common/export.h"
#include "render/resource/mesh_data.h"

#include <filesystem>

namespace Comet {
    class COMET_API MeshImporter final {
    public:
        [[nodiscard]] MeshData import(
            const std::filesystem::path& source_path) const;
    };
}
