#pragma once

#include "gameplay/navigation_mesh.h"
#include "gameplay/navigation_access.h"
#include "common/error.h"
#include "scene/entity.h"
#include <deque>
#include <functional>
#include <unordered_map>

namespace Comet::Gameplay {
    [[nodiscard]] COMET_API Result<std::vector<NavigationMesh::Link>> capture_navigation_links(
        Scene& scene);

    class COMET_API NavigationService final: public NavigationAccess {
    public:
        struct Statistics {
            uint64_t builds = 0;
            double build_milliseconds = 0;
            uint64_t product_loads = 0;
            double product_load_milliseconds = 0;
            double prepare_milliseconds = 0;
            uint64_t path_queries = 0;
            uint64_t surface_queries = 0;
            uint64_t obstacle_queries = 0;
            unsigned step_path_queries = 0;
            uint64_t obstacle_rebuilds = 0;
            double obstacle_rebuild_milliseconds = 0;
        };
        using GeometrySource =
            std::function<Result<std::vector<Math::Vec3>, Error>(Math::Vec3, Math::Vec3)>;
        using ProductSource = std::function<std::shared_ptr<const NavigationData>(AssetHandle)>;
        explicit NavigationService(GeometrySource geometry, ProductSource products = {});
        // 每固定步调用；路径缓存由运行域持有，脚本只提交目标与跑步意图。
        [[nodiscard]] Result<Status> move_to(
            Scene&, Entity, PhysicsAccess&, Math::Vec3 target, bool run = false) override;
        // 连接穿越期间由导航独占角色运动；动画仍采样姿态，不再覆盖弹道速度。
        [[nodiscard]] bool is_traversing_link(const Scene&, Entity) const override;
        [[nodiscard]] const NavigationMesh& mesh() const { return m_mesh; }
        // 构建发生在启动准备，查询累计于固定步；停止后保留本次运行统计。
        [[nodiscard]] const Statistics& get_statistics() const { return m_statistics; }

    private:
        friend class NavigationSystem;
        Result<void, Error> begin(Scene&);
        Result<void, Error> prepare_startup(Scene&);
        Result<void, Error> advance(Scene&, double total_time, double delta_time);
        void end() noexcept;
        struct Agent {
            EntityId entity = INVALID_ENTITY_ID;
            Math::Vec3 target{0};
            Math::Vec3 previous_position{0};
            NavigationMesh::Path path;
            size_t corner = 1;
            double next_query = 0;
            uint64_t last_step = 0;
            Status status = Status::NoPath;
            uint64_t mesh_generation = 0;
            bool query_pending = false;
            bool query_reserved = false;
            struct Jump {
                Math::Vec3 end, velocity;
                double deadline;
            };
            std::optional<Jump> jump;
        };
        GeometrySource m_geometry;
        ProductSource m_products;
        NavigationMesh m_mesh;
        NavigationRegionComponent m_settings;
        Math::Vec3 m_center{0};
        std::vector<Math::Vec3> m_geometry_snapshot;
        std::vector<NavigationMesh::Link> m_links;
        std::vector<NavigationMesh::Obstacle> m_obstacles;
        std::shared_ptr<const NavigationData> m_base_product;
        uint64_t m_mesh_generation = 0;
        std::unordered_map<EntityUuid, Agent> m_agents;
        std::deque<EntityUuid> m_pending_queries;
        unsigned m_reserved_queries = 0;
        Scene* m_scene = nullptr;
        double m_time = 0;
        float m_delta = 0;
        uint64_t m_step = 0;
        Statistics m_statistics;
        bool m_prepared = false;
        Result<void> prepare(Scene&);
        Result<void> synchronize_obstacles(Scene&);
        bool acquire_path_query(EntityUuid, Agent&);
        void cancel_path_query(EntityUuid, Agent&);
        Result<Math::Vec2> steer(Entity, PhysicsAccess&, Math::Vec3 position,
            Math::Vec3 destination, float speed, float scale);
    };
}
