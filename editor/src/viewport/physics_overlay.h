#pragma once

#include "physics/physics_service.h"

#include <map>
#include <tuple>

namespace Comet {
    class Scene;
    class LineDrawList;
}

namespace CometEditor {
    // Main 在帧捕获前读取呈现姿态；缓存只保存 CPU 边，不借用物理世界或 GPU 资源。
    class ColliderOverlay {
    public:
        explicit ColliderOverlay(Comet::PhysicsService::MeshSource meshes = {})
            : m_meshes(std::move(meshes)) {}
        void append(Comet::Scene& scene, Comet::LineDrawList& lines);
        void clear() { m_cache.clear(); }

    private:
        using Key = std::tuple<Comet::AssetHandle, Comet::ColliderShape, float, float, float>;
        struct Outline {
            uint64_t revision = 0;
            std::vector<Comet::PhysicsService::ColliderEdge> edges;
            bool used = false;
        };
        void append_mesh(const Comet::ColliderComponent& collider, Comet::Math::Vec3 scale,
            const Comet::Math::Mat4& matrix, Comet::Math::Vec4 color, Comet::LineDrawList& lines);
        Comet::PhysicsService::MeshSource m_meshes;
        std::map<Key, Outline> m_cache;
    };
    void append_joint_anchors(Comet::Scene& scene, Comet::LineDrawList& lines);
}
