#include "render/particle_collision_scene.h"
#include "common/binary.h"
#include <set>

namespace Comet {
    void ParticleCollisionBuilder::reset() {
        m_key.clear();
        m_topology_key.clear();
        m_meshes.clear();
        m_poses.clear();
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
            return Built::failure({"GPU 粒子网格碰撞最多支持 1024 个碰撞体"});
        Binary::Writer key, topology;
        for(const auto& collider : colliders) {
            topology.write_u64(collider.entity_id);
            topology.write_u64(collider.mesh.value());
            topology.write_u64(revision ? revision(collider.mesh) : 0);
            topology.write_u32(collider.layers);
            key.write_bytes(std::as_bytes(std::span(&collider.model, 1)));
            key.write_u64(reinterpret_cast<uintptr_t>(collider.animation.get()));
            key.write_bytes(std::as_bytes(std::span(&collider.bend, 1)));
        }
        key.write_bytes(topology.data());
        if(m_scene && m_key == key.data())
            return Built::success(m_scene);
        std::set<AssetHandle> used_meshes;
        std::set<uint64_t> used_entities;
        std::vector<std::span<const MeshVertex>> vertices;
        size_t deformed_vertices = 0;
        for(const auto& collider : colliders) {
            if(!collider.mesh || !Math::positive_trs_scale(collider.model))
                return Built::failure({"GPU 粒子网格碰撞需要有效网格与正 TRS 变换"});
            const auto version = revision ? revision(collider.mesh) : 0;
            auto found = m_meshes.find(collider.mesh);
            bool reloaded = found == m_meshes.end() || found->second.revision != version;
            if(reloaded) {
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
            used_meshes.insert(collider.mesh);
            used_entities.insert(collider.entity_id);
            const auto& mesh = found->second.data;
            if(mesh.vertices.size() > 65536)
                return Built::failure({"单个粒子碰撞网格最多支持 65536 个顶点"});
            auto pose = m_poses.find(collider.entity_id);
            if(reloaded || pose == m_poses.end() || pose->second.playback != collider.animation
                || pose->second.bend != collider.bend) {
                auto sampled = RayScene::deform(mesh, collider.animation, collider.bend);
                if(!sampled)
                    return Built::failure({sampled.error()});
                pose =
                    m_poses.insert_or_assign(collider.entity_id, std::move(sampled).value()).first;
            }
            if(pose->second.vertices) {
                deformed_vertices += pose->second.vertices->size();
                if(deformed_vertices > 262144)
                    return Built::failure({"粒子动态几何超过 262144 个顶点预算"});
                vertices.emplace_back(*pose->second.vertices);
            } else
                vertices.emplace_back(mesh.vertices);
        }
        const bool refit = m_scene && m_topology_key == topology.data();
        auto geometry = refit ? std::make_shared<RayScene::Geometry>(*m_scene->geometry)
                              : std::make_shared<RayScene::Geometry>();
        if(!refit) {
            for(size_t object = 0; object < colliders.size(); ++object) {
                const auto& collider = colliders[object];
                const auto& mesh = m_meshes.at(collider.mesh).data;
                const size_t count =
                    mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size();
                if(count % 3 || count / 3 > MAX_TRIANGLES - geometry->triangles.size())
                    return Built::failure({"GPU 粒子网格碰撞超过 65536 个三角形预算或索引不完整"});
                for(size_t first = 0; first < count; first += 3) {
                    RayScene::Triangle triangle{};
                    std::array<uint32_t, 3> indices{};
                    for(size_t corner = 0; corner < 3; ++corner) {
                        indices[corner] = mesh.indices.empty() ? uint32_t(first + corner)
                                                               : mesh.indices[first + corner];
                        if(indices[corner] >= mesh.vertices.size())
                            return Built::failure({"GPU 粒子碰撞网格索引无效"});
                    }
                    // GPU 只读取 layers；其余槽保存 refit 的物体与源索引，BVH 重排时一起移动。
                    triangle.surface = {collider.layers, uint32_t(object),
                        indices[0] | (indices[1] << 16), indices[2]};
                    geometry->triangles.push_back(triangle);
                }
            }
        }
        for(auto& triangle : geometry->triangles) {
            const auto object = triangle.surface[1];
            const std::array<uint32_t, 3> indices{
                triangle.surface[2] & 0xffffu, triangle.surface[2] >> 16, triangle.surface[3]};
            for(size_t corner = 0; corner < 3; ++corner) {
                triangle.positions[corner] =
                    colliders[object].model
                    * Math::Vec4(vertices[object][indices[corner]].position, 1);
                if(!Math::is_finite(triangle.positions[corner]))
                    return Built::failure({"GPU 粒子碰撞变形包含非有限位置"});
            }
        }
        if(refit)
            geometry->refit_bvh();
        else
            geometry->build_bvh();
        // links.z 保存子树碰撞层并集，供 GPU 提前跳过无关节点。
        for(size_t i = geometry->nodes.size(); i-- > 0;) {
            auto& node = geometry->nodes[i];
            node.links[2] = 0;
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
        std::erase_if(
            m_meshes, [&](const auto& entry) { return !used_meshes.contains(entry.first); });
        std::erase_if(
            m_poses, [&](const auto& entry) { return !used_entities.contains(entry.first); });
        m_key = key.data();
        m_topology_key = topology.data();
        m_scene = std::move(scene);
        return Built::success(m_scene);
    }
}
