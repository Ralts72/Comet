#pragma once

#include "asset/artifact/mesh_artifact.h"
#include "asset/database.h"
#include "asset/data/material_data.h"
#include "asset/data/texture_data.h"
#include "asset/data/environment_data.h"
#include <variant>

namespace Comet {
    struct MeshArtifactCandidate {
        AssetHandle handle;
        AssetRevision revision = INVALID_ASSET_REVISION;
        std::filesystem::path relative_path;
        Result<MeshArtifact> result;
        bool reused_artifact = false;
    };
    struct TextureImportCandidate {
        AssetHandle handle;
        AssetRevision revision = INVALID_ASSET_REVISION;
        std::filesystem::path relative_path;
        Result<TextureData> result;
    };
    struct MaterialImportCandidate {
        AssetRecord record;
        AssetRevision revision = INVALID_ASSET_REVISION;
        Result<MaterialData> result;
    };
    struct EnvironmentImportCandidate {
        AssetHandle handle;
        AssetRevision revision = INVALID_ASSET_REVISION;
        std::filesystem::path relative_path;
        Result<EnvironmentData> result;
    };
    struct AssetImportResult {
        std::variant<std::monostate, MeshArtifactCandidate, TextureImportCandidate,
            MaterialImportCandidate, EnvironmentImportCandidate>
            candidate;
    };
}
