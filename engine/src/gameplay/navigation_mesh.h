#pragma once

#include "asset/content_fwd.h"

#include "common/result.h"
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
        struct Link {
            Math::Vec3 start, end;
            float radius = 0.5f;
            bool bidirectional = true;
        };
        struct Obstacle {
            Math::Vec3 minimum, maximum;
            bool operator==(const Obstacle&) const = default;
        };
        struct Path {
            struct Jump {
                std::size_t corner;
                Math::Vec3 end;
                float radius;
            };
            Status status = Status::NoPath;
            std::vector<Math::Vec3> points;
            std::vector<Jump> jumps;
        };
        NavigationMesh();
        ~NavigationMesh();
        NavigationMesh(const NavigationMesh&) = delete;
        NavigationMesh& operator=(const NavigationMesh&) = delete;
        Result<void> build(std::span<const Math::Vec3> triangles, Math::Vec3 center,
            const NavigationRegionComponent& settings, std::span<const Link> links = {},
            std::span<const Obstacle> obstacles = {});
        [[nodiscard]] static Result<uint64_t> input_fingerprint(
            std::span<const Math::Vec3> triangles, Math::Vec3 center,
            const NavigationRegionComponent& settings, std::span<const Link> links = {});
        [[nodiscard]] Result<NavigationData> product(uint64_t input_fingerprint) const;
        [[nodiscard]] Result<void> load(const NavigationData& product);
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
