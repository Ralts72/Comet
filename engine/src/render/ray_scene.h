#pragma once

#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"
#include "common/error.h"
#include "common/result.h"
#include "render/lighting.h"

#include <functional>
#include <memory>
#include <vector>

namespace Comet {
    struct RenderWorld;

    // Main 构造的只读世界空间快照；不持有 Scene、资产服务或 GPU 对象。
    struct RayScene {
        static constexpr uint32_t MAX_SKINNED_VERTICES = 65536;
        static constexpr uint32_t MAX_TRIANGLES = 262144;
        static constexpr size_t MAX_TEXTURE_BYTES = 128 * 1024 * 1024;
        struct Triangle {
            Math::Vec4 positions[3];
            // normal.w、tangent.w 分别保存顶点 UV 的 u、v。
            Math::Vec4 normals[3];
            Math::Vec4 tangents[3];
            Math::Vec4 bitangents[3];
            // 材质／物体槽、三个源顶点索引；GPU 只读取 x。
            std::array<uint32_t, 4> surface{};
        };
        struct Surface {
            Math::Vec4 albedo_metallic;
            Math::Vec4 emission_roughness;
            // base color、normal、metallic/roughness、emissive；0 表示默认贴图。
            std::array<uint32_t, 4> textures{};
            Math::Vec4 parameters{};          // normal scale、double sided、transmission、IOR
            Math::Vec4 opacity{1, .5f, 0, 0}; // alpha factor、cutoff、AlphaMode，保留
        };
        struct Texture {
            std::array<uint32_t, 4> extent{};  // texel offset、width、height，保留
            std::array<uint32_t, 4> sampler{}; // address u、address v、filter，保留
        };
        struct Node {
            Math::Vec4 minimum;
            Math::Vec4 maximum;
            // 叶：first/count；内部节点 count=0；escape 是整棵子树之后的节点。
            std::array<uint32_t, 4> links{};
        };
        struct Geometry {
            struct Topology {
                std::vector<std::shared_ptr<const MeshData>> meshes;
            };
            // 变换更新复用源顶点和三角形顺序；旧快照仍拥有独立的世界坐标与 BVH。
            std::shared_ptr<const Topology> topology;
            std::vector<Math::Mat4> transforms;
            struct Deformation {
                std::shared_ptr<const AnimationPlayback> playback;
                std::shared_ptr<const std::vector<MeshVertex>> vertices;
            };
            std::vector<Deformation> deformations;
            std::vector<Triangle> triangles;
            std::vector<Node> nodes;
        };
        struct Materials {
            std::vector<Surface> surfaces;
            std::vector<Texture> textures;
            std::vector<Math::Vec4> texels;
        };
        struct Environment {
            Math::Vec2u size{};
            std::vector<Math::Vec4> texels;
        };
        std::shared_ptr<const Geometry> geometry;
        std::shared_ptr<const Materials> materials;
        std::shared_ptr<const Environment> environment_map;
        std::shared_ptr<const std::vector<uint32_t>> emitters;
        std::shared_ptr<const LightingData> lighting;
        Math::Vec3 background{};
        // 背景强度（-1 使用纯色背景）、照明强度、rotation sin/cos。
        Math::Vec4 environment{-1, 0, 0, 1};
    };

    // 主线程按内容／已发布 CPU 版本重建，普通 raster 路径不读取或保留 CPU 几何。
    class COMET_API RaySceneBuilder {
    public:
        explicit RaySceneBuilder(uint32_t texture_dimension = 8192)
            : m_texture_dimension(texture_dimension) {}
        using MeshReader = std::function<Result<MeshData, Error>(AssetHandle)>;
        using MeshRevision = std::function<uint64_t(AssetHandle)>;
        struct Images {
            using Reader = std::function<Result<TextureData, Error>(AssetHandle)>;
            Reader texture;
            Reader environment;
            MeshRevision revision;
        };
        [[nodiscard]] Result<std::shared_ptr<const RayScene>> update(const RenderWorld& world,
            const MeshReader& read, const MeshRevision& revision = {}, const Images& images = {});
        struct Statistics {
            uint64_t geometry_builds = 0;
            uint64_t geometry_refits = 0;
            uint64_t material_builds = 0;
            uint64_t environment_builds = 0;
        };
        [[nodiscard]] Statistics statistics() const { return m_statistics; }
        void reset();

    private:
        uint32_t m_texture_dimension;
        std::vector<std::byte> m_settings_key;
        std::vector<std::byte> m_geometry_key;
        std::vector<std::byte> m_topology_key;
        std::vector<std::byte> m_material_key;
        std::vector<std::byte> m_environment_key;
        std::vector<std::byte> m_lighting_key;
        std::shared_ptr<const RayScene::Geometry> m_geometry;
        std::shared_ptr<const RayScene::Materials> m_materials;
        std::shared_ptr<const RayScene::Environment> m_environment;
        std::shared_ptr<const LightingData> m_lighting;
        Statistics m_statistics;
        std::shared_ptr<const RayScene> m_scene;
        std::string m_error;
    };
}
