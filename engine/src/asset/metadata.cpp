#include "asset/metadata.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Comet {
    namespace {
        std::string lowercase(std::string text) {
            std::ranges::transform(text, text.begin(),
                [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return text;
        }
    }

    std::string_view to_string(const AssetType type) noexcept {
        switch(type) {
            case AssetType::AnimationController:
                return "animation_controller";
            case AssetType::Prefab:
                return "prefab";
            case AssetType::Model:
                return "model";
            case AssetType::Audio:
                return "audio";
            case AssetType::Script:
                return "script";
            case AssetType::Environment:
                return "environment";
            case AssetType::Texture:
                return "texture";
            case AssetType::Material:
                return "material";
            case AssetType::Mesh:
                return "mesh";
            case AssetType::Shader:
                return "shader";
            case AssetType::ShaderProgram:
                return "shader_program";
            case AssetType::Scene:
                return "scene";
            case AssetType::Unknown:
                return "unknown";
        }
        return "unknown";
    }

    std::optional<AssetType> asset_type_from_string(const std::string_view value) noexcept {
        if(value == "animation_controller")
            return AssetType::AnimationController;
        if(value == "prefab")
            return AssetType::Prefab;
        if(value == "model")
            return AssetType::Model;
        if(value == "audio")
            return AssetType::Audio;
        if(value == "script")
            return AssetType::Script;
        if(value == "environment")
            return AssetType::Environment;
        if(value == "texture")
            return AssetType::Texture;
        if(value == "material")
            return AssetType::Material;
        if(value == "mesh")
            return AssetType::Mesh;
        if(value == "shader")
            return AssetType::Shader;
        if(value == "shader_program")
            return AssetType::ShaderProgram;
        if(value == "scene")
            return AssetType::Scene;
        return std::nullopt;
    }

    AssetImportSettings make_default_import_settings(const AssetType type) {
        if(type == AssetType::Texture) {
            return TextureImportSettings{};
        }
        return std::monostate{};
    }

    std::string source_extension(const std::filesystem::path& path) {
        return lowercase(path.extension().string());
    }

    bool is_lua_module_source(const std::filesystem::path& path) {
        return lowercase(path.filename().string()).ends_with(".module.lua");
    }

    bool is_source_only_path(const std::filesystem::path& path) {
        const auto extension = source_extension(path);
        if(extension == ".bin" || extension == ".glsl" || extension == ".rml"
            || extension == ".rcss")
            return true;
        if(extension != ".lua")
            return false;
        const auto name = lowercase(path.filename().string());
        return name.ends_with(".ui.lua") || name.ends_with(".module.lua");
    }

    std::optional<AssetType> asset_type_from_path(const std::filesystem::path& path) {
        const auto extension = source_extension(path);
        if(extension == ".hdr")
            return AssetType::Environment;
        if(extension == ".png" || extension == ".jpg" || extension == ".jpeg")
            return AssetType::Texture;
        if(extension == ".mat")
            return AssetType::Material;
        if(extension == ".gltf" || extension == ".glb")
            return AssetType::Model;
        if(extension == ".vert" || extension == ".frag" || extension == ".comp"
            || extension == ".geom")
            return AssetType::Shader;
        if(extension == ".shader")
            return AssetType::ShaderProgram;
        if(extension == ".lua" && !is_source_only_path(path))
            return AssetType::Script;
        if(extension == ".wav")
            return AssetType::Audio;
        if(extension == ".ani")
            return AssetType::AnimationController;
        if(extension == ".prefab")
            return AssetType::Prefab;
        if(extension == ".scene")
            return AssetType::Scene;
        return std::nullopt;
    }

    std::filesystem::path metadata_path(const std::filesystem::path& asset_path) {
        std::filesystem::path result = asset_path;
        result += ".meta";
        return result;
    }
}
