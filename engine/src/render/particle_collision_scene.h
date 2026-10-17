#pragma once
#include "render/ray_scene.h"
#include "particles/particle_data.h"
#include <map>
namespace Comet {
    // Main 上从静态碰撞体构建；不读取材质、光源或蒙皮姿态。
    class COMET_API ParticleCollisionBuilder {
    public:
        static constexpr uint32_t MAX_TRIANGLES = 65536;
        Result<std::shared_ptr<const RayScene>, Error> update(uint64_t lifetime,
            std::span<const ParticleMeshCollider>, const RaySceneBuilder::MeshReader&,
            const RaySceneBuilder::MeshRevision& = {});
        void reset();

    private:
        std::vector<std::byte> m_key;
        struct Source {
            uint64_t revision;
            MeshData data;
        };
        std::map<AssetHandle, Source> m_meshes;
        std::shared_ptr<const RayScene> m_scene;
        uint64_t m_lifetime = 0;
    };
}
