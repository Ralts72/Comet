#pragma once

#include "asset/data/navigation_data.h"
#include "physics/physics_service.h"
#include "scene/components.h"

namespace Comet {
    class Scene;
    class ComponentRegistry;
}
namespace Comet::NavigationBake {
    struct Input {
        std::vector<Math::Vec3> triangles;
        Math::Vec3 center;
        NavigationRegionComponent settings;
        uint64_t fingerprint = 0;
    };
    // Main 在独立内容副本中准备真实碰撞形状；不推进脚本、动画或模拟。
    [[nodiscard]] Result<Input, Error> capture(Scene& scene, const ComponentRegistry& components,
        EntityUuid region, PhysicsService::MeshSource meshes = {});
    // 后台仅消费自有 CPU 输入；生成的产品不持有 Scene 或物理世界。
    [[nodiscard]] Result<NavigationData> bake(const Input& input);
}
