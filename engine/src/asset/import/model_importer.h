#pragma once

#include "asset/import/mesh_importer.h"
#include "asset/data/material_data.h"
#include "asset/data/model_data.h"
#include "asset/metadata.h"
#include "asset/import_settings.h"
#include "graphics/enums.h"

#include <limits>

namespace Comet {
    // CPU 导入候选以 glTF 下标关联子资源；发布时统一分配持久 AssetHandle。
    struct ModelImportData {
        static constexpr uint32_t NONE = std::numeric_limits<uint32_t>::max();
        struct Mesh {
            std::string name;
            uint32_t source_mesh;
            uint32_t source_primitive;
            std::optional<uint32_t> skin_node;
            uint32_t material = NONE;
            MeshData data;
            std::array<uint32_t, 2> lods{NONE, NONE};
            bool collision = false;
        };
        struct Node {
            std::string name;
            uint32_t parent = NONE;
            JointTransform transform;
            std::vector<uint32_t> meshes;
        };
        struct Scene {
            std::string name;
            std::vector<uint32_t> roots;
        };
        struct TextureBinding {
            std::string property;
            uint32_t texture;
            TextureImportSettings settings;
        };
        struct Material {
            std::string name;
            MaterialData data;
            std::vector<TextureBinding> textures;
        };
        struct Texture {
            std::string name;
            uint32_t image;
            SamplerDesc sampler;
            bool mipmaps = true;
        };
        struct Image {
            std::string name;
            std::vector<std::byte> encoded;
        };

        std::vector<Node> nodes;
        std::vector<Scene> scenes;
        uint32_t default_scene = 0;
        std::vector<Mesh> meshes;
        std::vector<Material> materials;
        std::vector<Texture> textures;
        std::vector<Image> images;
        std::vector<std::filesystem::path> source_dependencies;
    };

    // 发布候选拥有几何和图片；纹理逐项解码发布，避免同时驻留全部解码副本。
    struct ModelImportProduct {
        struct Mesh {
            AssetHandle handle;
            MeshData data;
        };
        struct Material {
            AssetHandle handle;
            MaterialData data;
        };
        struct Texture {
            AssetHandle handle;
            uint32_t image;
            TextureImportSettings settings;
            SamplerDesc sampler;
        };
        AssetMetadata metadata;
        ModelData model;
        std::vector<Mesh> meshes;
        std::vector<Material> materials;
        std::vector<Texture> textures;
        std::vector<ModelImportData::Image> images;
        std::vector<std::filesystem::path> source_dependencies;
    };

    class COMET_API ModelImporter final {
    public:
        struct SourceInfo {
            std::size_t working_bytes;
            std::vector<std::filesystem::path> dependencies;
        };
        [[nodiscard]] static Result<SourceInfo> inspect_source(
            const std::filesystem::path& source, const AssetImportLimits& limits = {});
        [[nodiscard]] static Result<std::size_t> working_bytes(
            const std::filesystem::path& source, const AssetImportLimits& limits = {});
        [[nodiscard]] Result<ModelImportData> import(const std::filesystem::path& source,
            std::size_t memory_budget = AssetImportLimits{}.mesh_working_bytes,
            const AssetImportLimits& limits = {}) const;
        [[nodiscard]] static Result<ModelImportProduct> bind(
            ModelImportData imported, const AssetMetadata& previous);
    };
}
