#pragma once

#include "common/result.h"
#include "common/export.h"
#include "core/math_utils.h"
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Comet {
    struct NavigationRegionComponent;
}
namespace Comet::Gameplay {
    // CPU 导航几何与查询；不持有场景、物理或角色状态。
    class COMET_API NavigationMesh {
    public:
        enum class Status { Complete, NoPath, BudgetExceeded };
        struct Path {
            Status status = Status::NoPath;
            std::vector<Math::Vec3> points;
        };
        NavigationMesh();
        ~NavigationMesh();
        NavigationMesh(const NavigationMesh&) = delete;
        NavigationMesh& operator=(const NavigationMesh&) = delete;
        Result<void> build(std::span<const Math::Vec3> triangles, Math::Vec3 center,
            const NavigationRegionComponent& settings);
        void clear();
        [[nodiscard]] Result<Path> find_path(Math::Vec3 start, Math::Vec3 target) const;
        [[nodiscard]] std::optional<Math::Vec3> surface_step(
            Math::Vec3 start, Math::Vec3 target) const;
        [[nodiscard]] bool ready() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
