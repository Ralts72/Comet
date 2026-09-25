#pragma once

#include "asset/handle.h"
#include "asset/import/input_snapshot.h"
#include "common/export.h"
#include "common/result.h"
#include "asset/data/mesh_data.h"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <vector>

namespace Comet {
    class COMET_API MeshArtifact final {
    public:
        [[nodiscard]] static std::optional<MeshArtifact> load(
            const std::filesystem::path& artifact_path, AssetHandle expected_handle,
            std::size_t memory_budget = std::numeric_limits<std::size_t>::max());
        [[nodiscard]] Result<void> publish_atomic(const std::filesystem::path& artifact_path) const;
        [[nodiscard]] std::vector<std::filesystem::path> source_dependencies() const;

        AssetHandle handle;
        std::uint32_t importer_version = 0;
        ImportInputSnapshot source_inputs;
        MeshData data;
    };
}
