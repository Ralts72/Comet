#pragma once

#include "asset/import/model_importer.h"
#include "asset/import/input_snapshot.h"

#include <map>
#include <optional>

namespace Comet {
    class COMET_API ModelArtifact final {
    public:
        static constexpr uint32_t IMPORTER_VERSION = 1;

        [[nodiscard]] static Result<ModelArtifact> stage(const std::filesystem::path& index_path,
            ModelImportProduct product, ImportInputSnapshot inputs,
            const AssetImportLimits& limits = {});
        [[nodiscard]] Result<void> publish_atomic(const std::filesystem::path& index_path) const;
        [[nodiscard]] static std::optional<ModelArtifact> load(
            const std::filesystem::path& index_path, AssetHandle expected_handle);
        [[nodiscard]] std::filesystem::path product_path(
            const std::filesystem::path& index_path, AssetHandle handle) const;

        AssetMetadata metadata;
        ModelData model;
        ImportInputSnapshot source_inputs;
        std::map<AssetHandle, SamplerDesc> texture_samplers;
        uint32_t importer_version = IMPORTER_VERSION;
        uint64_t generation = 0;
    };
}
