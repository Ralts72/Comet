#pragma once

#include "asset/handle.h"
#include "asset/import/input_snapshot.h"
#include "common/export.h"
#include "render/resource/mesh_data.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Comet {
    class COMET_API MeshArtifact final {
    public:
        [[nodiscard]] static std::optional<MeshArtifact> load(
            const std::filesystem::path& artifact_path, AssetHandle expected_handle);
        void publish_atomic(const std::filesystem::path& artifact_path) const;
        [[nodiscard]] std::vector<std::filesystem::path> source_dependencies() const;

        AssetHandle handle;
        std::uint32_t importer_version = 0;
        ImportInputSnapshot source_inputs;
        MeshData data;
    };
}
