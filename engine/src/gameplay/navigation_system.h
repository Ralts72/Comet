#pragma once

#include "gameplay/navigation_mesh.h"
#include "scene/systems/system.h"
#include "scene/entity.h"
#include <functional>
#include <unordered_map>

namespace Comet::Gameplay {
    [[nodiscard]] COMET_API Result<std::vector<NavigationMesh::Link>> capture_navigation_links(
        Scene& scene);

    class COMET_API NavigationSystem final: public System {
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
        enum class Status { Moving, Arrived, NoPath, Blocked, BudgetExceeded, Disabled };
        explicit NavigationSystem(GeometrySource geometry, ProductSource products = {});
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> on_runtime_ready(
            Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> fixed_update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;
        // 每固定步调用；路径缓存由运行域持有，脚本只提交目标与跑步意图。
        [[nodiscard]] Result<Status> move_to(
            Scene&, Entity, PhysicsAccess&, Math::Vec3 target, bool run = false);
        [[nodiscard]] const NavigationMesh& mesh() const { return m_mesh; }
        // 构建发生在启动准备，查询累计于固定步；停止后保留本次运行统计。
        [[nodiscard]] const Statistics& get_statistics() const { return m_statistics; }
        [[nodiscard]] static const char* name(Status);

    private:
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
        Scene* m_scene = nullptr;
        double m_time = 0;
        float m_delta = 0;
        uint64_t m_step = 0;
        Statistics m_statistics;
        bool m_prepared = false;
        Result<void> prepare(Scene&);
        Result<void> synchronize_obstacles(Scene&);
    };
}
