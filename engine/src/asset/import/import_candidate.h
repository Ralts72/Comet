#pragma once

#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/database.h"
#include "asset/data/material_data.h"
#include "asset/data/texture_data.h"
#include "asset/data/environment_data.h"
#include <string>
#include <variant>
#include <vector>

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
    struct ShaderProgramImportSource {
        AssetHandle handle;
        AssetRevision revision = INVALID_ASSET_REVISION;
        std::filesystem::path path;
        std::string entry;
    };
    struct ShaderProgramImportRequest {
        AssetHandle handle;
        AssetRevision revision = INVALID_ASSET_REVISION;
        ShaderProgramImportSource vertex;
        ShaderProgramImportSource fragment;
        std::filesystem::path descriptor_path;
        std::optional<ShaderProgramMaterial> material;
    };
    struct ShaderProgramImportFailure {
        std::string message;
        std::vector<std::filesystem::path> dependencies;
    };
    struct ShaderProgramImportPrepared {
        ShaderProgramArtifact artifact;
        bool from_cache = false;
    };
    struct ShaderProgramImportCandidate {
        ShaderProgramImportRequest request;
        Result<ShaderProgramImportPrepared, ShaderProgramImportFailure> result;
    };
    struct AssetImportResult {
        std::variant<std::monostate, MeshArtifactCandidate, TextureImportCandidate,
            MaterialImportCandidate, EnvironmentImportCandidate, ShaderProgramImportCandidate>
            candidate;
    };
}
