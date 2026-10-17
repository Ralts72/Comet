#include "render/particle_collision_scene.h"
#include "common/binary.h"
#include <set>
namespace Comet {
    void ParticleCollisionBuilder::reset() {
        m_key.clear();
        m_meshes.clear();
        m_scene.reset();
        m_lifetime = 0;
    }
    Result<std::shared_ptr<const RayScene>, Error> ParticleCollisionBuilder::update(
        uint64_t lifetime, std::span<const ParticleMeshCollider> colliders,
        const RaySceneBuilder::MeshReader& read, const RaySceneBuilder::MeshRevision& revision) {
        using Built = Result<std::shared_ptr<const RayScene>, Error>;
        if(m_lifetime != lifetime) {
            reset();
            m_lifetime = lifetime;
        }
        if(colliders.size() > 1024)
            return Built::failure({"GPU 粒子网格碰撞最多支持 1024 个静态碰撞体"});
        Binary::Writer key;
        for(const auto& collider : colliders) {
            key.write_u64(collider.entity_id);
            key.write_u64(collider.mesh.value());
            key.write_u64(revision ? revision(collider.mesh) : 0);
            key.write_u32(collider.layers);
            key.write_bytes(std::as_bytes(std::span(&collider.model, 1)));
        }
        if(m_scene && m_key == key.data())
            return Built::success(m_scene);
        auto geometry = std::make_shared<RayScene::Geometry>();
        std::set<AssetHandle> used;
        for(const auto& collider : colliders) {
            if(!collider.mesh || !Math::positive_trs_scale(collider.model))
                return Built::failure({"GPU 粒子静态网格碰撞需要有效网格与正 TRS 变换"});
            const auto version = revision ? revision(collider.mesh) : 0;
            auto found = m_meshes.find(collider.mesh);
            if(found == m_meshes.end() || found->second.revision != version) {
                if(!read)
                    return Built::failure({"GPU 粒子网格碰撞没有 CPU 几何读取服务"});
                auto loaded = read(collider.mesh);
                if(!loaded)
                    return Built::failure(loaded.error());
                found =
                    m_meshes
                        .insert_or_assign(collider.mesh, Source{version, std::move(loaded).value()})
                        .first;
            }
            used.insert(collider.mesh);
            const auto& mesh = found->second.data;
            const size_t count = mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size();
            if(count % 3 || count / 3 > MAX_TRIANGLES - geometry->triangles.size())
                return Built::failure({"GPU 粒子网格碰撞超过 65536 个三角形预算或索引不完整"});
            for(size_t first = 0; first < count; first += 3) {
                RayScene::Triangle triangle{};
                triangle.surface[0] = collider.layers;
                for(size_t corner = 0; corner < 3; ++corner) {
                    const auto index =
                        mesh.indices.empty() ? first + corner : mesh.indices[first + corner];
                    if(index >= mesh.vertices.size()
                        || !Math::is_finite(mesh.vertices[index].position))
                        return Built::failure({"GPU 粒子碰撞网格包含无效顶点或索引"});
                    triangle.positions[corner] =
                        collider.model * Math::Vec4(mesh.vertices[index].position, 1);
                    if(!Math::is_finite(triangle.positions[corner]))
                        return Built::failure({"GPU 粒子碰撞网格变换产生非有限位置"});
                }
                const auto edge = Math::Vec3(triangle.positions[1] - triangle.positions[0]);
                const auto other = Math::Vec3(triangle.positions[2] - triangle.positions[0]);
                if(glm::length(glm::cross(edge, other)) > 1e-8f)
                    geometry->triangles.push_back(triangle);
            }
        }
        geometry->build_bvh();
        // links.z 在碰撞 BVH 中保存子树碰撞层并集，提前跳过完全不相关的层。
        for(size_t i = geometry->nodes.size(); i-- > 0;) {
            auto& node = geometry->nodes[i];
            if(node.links[1]) {
                for(uint32_t item = 0; item < node.links[1]; ++item)
                    node.links[2] |= geometry->triangles[node.links[0] + item].surface[0];
            } else {
                const auto& left = geometry->nodes[i + 1];
                node.links[2] = left.links[2] | geometry->nodes[left.links[3]].links[2];
            }
        }
        auto scene = std::make_shared<RayScene>();
        scene->geometry = std::move(geometry);
        std::erase_if(m_meshes, [&](const auto& entry) { return !used.contains(entry.first); });
        m_key = key.data();
        m_scene = std::move(scene);
        return Built::success(m_scene);
    }
}
