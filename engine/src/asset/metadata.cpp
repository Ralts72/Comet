#include "asset/metadata.h"

namespace Comet {
    std::string_view to_string(const AssetType type) noexcept {
        switch(type) {
            case AssetType::Texture: return "texture";
            case AssetType::Material: return "material";
            case AssetType::Mesh: return "mesh";
            case AssetType::Shader: return "shader";
            case AssetType::Scene: return "scene";
            case AssetType::Unknown: return "unknown";
        }
        return "unknown";
    }

    std::optional<AssetType> asset_type_from_string(
        const std::string_view value) noexcept {
        if(value == "texture") return AssetType::Texture;
        if(value == "material") return AssetType::Material;
        if(value == "mesh") return AssetType::Mesh;
        if(value == "shader") return AssetType::Shader;
        if(value == "scene") return AssetType::Scene;
        return std::nullopt;
    }

    std::filesystem::path metadata_path(
        const std::filesystem::path& asset_path) {
        std::filesystem::path result = asset_path;
        result += ".meta";
        return result;
    }
}
