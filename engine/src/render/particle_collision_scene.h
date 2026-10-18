#pragma once
#include "render/ray_scene.h"
#include "particles/particle_data.h"
#include <map>
namespace Comet {
    // Main 上从物理／视觉碰撞快照构建；共用射线变形与 BVH refit，不读取材质或光源。
    class COMET_API ParticleCollisionBuilder {
    public:
        static constexpr uint32_t MAX_TRIANGLES = 65536;
        Result<std::shared_ptr<const RayScene>, Error> update(uint64_t lifetime,
            std::span<const ParticleMeshCollider>, const RaySceneBuilder::MeshReader&,
            const RaySceneBuilder::MeshRevision& = {});
        void reset();

    private:
        std::vector<std::byte> m_key, m_topology_key;
        struct Source {
            uint64_t revision;
            MeshData data;
        };
        std::map<AssetHandle, Source> m_meshes;
        std::map<uint64_t, RayScene::Geometry::Deformation> m_poses;
        std::shared_ptr<const RayScene> m_scene;
        uint64_t m_lifetime = 0;
    };
}
