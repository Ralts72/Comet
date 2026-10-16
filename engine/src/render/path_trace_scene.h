#pragma once

#include "asset/data/mesh_data.h"
#include "common/error.h"
#include "common/result.h"
#include "render/lighting.h"
#include "render/scene/render_scene.h"

#include <functional>
#include <memory>
#include <vector>

namespace Comet {
    struct RenderWorld;

    // Main 构造的只读世界空间快照；不持有 Scene、资产服务或 GPU 对象。
    struct PathTraceScene {
        static constexpr uint32_t MAX_TRIANGLES = 262144;
        struct Triangle {
            Math::Vec4 positions[3];
            Math::Vec4 normals[3];
            Math::Vec4 albedo_metallic;
            Math::Vec4 emission_roughness;
        };
        struct Node {
            Math::Vec4 minimum;
            Math::Vec4 maximum;
            // 叶：first/count；内部节点 count=0；escape 是整棵子树之后的节点。
            std::array<uint32_t, 4> links{};
        };
        std::vector<Triangle> triangles;
        std::vector<Node> nodes;
        std::vector<uint32_t> emitters;
        LightingData lighting;
        Math::Vec3 background{};
    };

    // 主线程按内容／已发布 CPU 版本重建，普通 raster 路径不读取或保留 CPU 几何。
    class COMET_API PathTraceSceneBuilder {
    public:
        using MeshReader = std::function<Result<MeshData, Error>(AssetHandle)>;
        using MeshRevision = std::function<uint64_t(AssetHandle)>;
        [[nodiscard]] Result<std::shared_ptr<const PathTraceScene>> update(
            const RenderWorld& world, const MeshReader& read, const MeshRevision& revision = {});
        void reset();

    private:
        std::vector<std::byte> m_key;
        std::shared_ptr<const PathTraceScene> m_scene;
        std::string m_error;
    };
}
