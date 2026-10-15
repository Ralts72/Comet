#pragma once

#include "gameplay/navigation_mesh.h"
#include "scene/systems/system.h"
#include "scene/entity.h"
#include <functional>
#include <unordered_map>

namespace Comet::Gameplay {
    class COMET_API NavigationSystem final: public System {
    public:
        using GeometrySource =
            std::function<Result<std::vector<Math::Vec3>, Error>(Math::Vec3, Math::Vec3)>;
        enum class Status { Moving, Arrived, NoPath, Blocked, BudgetExceeded, Disabled };
        explicit NavigationSystem(GeometrySource geometry);
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> fixed_update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;
        // 每固定步调用；路径缓存由运行域持有，脚本只提交目标与跑步意图。
        [[nodiscard]] Result<Status> move_to(
            Scene&, Entity, PhysicsAccess&, Math::Vec3 target, bool run = false);
        [[nodiscard]] const NavigationMesh& mesh() const { return m_mesh; }
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
        };
        GeometrySource m_geometry;
        NavigationMesh m_mesh;
        NavigationRegionComponent m_settings;
        std::unordered_map<EntityUuid, Agent> m_agents;
        Scene* m_scene = nullptr;
        double m_time = 0;
        float m_delta = 0;
        uint64_t m_step = 0;
        unsigned m_queries = 0;
        bool m_prepared = false;
        Result<void> prepare(Scene&);
    };
}
