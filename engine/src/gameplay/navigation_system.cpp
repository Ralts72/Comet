#include "gameplay/navigation_system.h"
#include "gameplay/character_controller.h"
#include "physics/physics_access.h"
#include "scene/scene.h"
#include <algorithm>
#include <cmath>

namespace Comet::Gameplay {
    namespace {
        float planar_distance(Math::Vec3 a, Math::Vec3 b) {
            return std::hypot(a.x - b.x, a.z - b.z);
        }
    }
    NavigationSystem::NavigationSystem(GeometrySource geometry) : m_geometry(std::move(geometry)) {}
    Result<void, Error> NavigationSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices&) {
        m_scene = &scene;
        m_prepared = false;
        m_agents.clear();
        m_mesh.clear();
        m_step = 0;
        m_delta = 0;
        return Result<void, Error>::success();
    }
    Result<void> NavigationSystem::prepare(Scene& scene) {
        if(scene.component_count<NavigationRegionComponent>() == 0)
            return Result<void>::success();
        if(scene.component_count<NavigationRegionComponent>() != 1)
            return Result<void>::failure("Only one navigation region per scene is supported");
        Math::Vec3 center{0};
        scene.each<NavigationRegionComponent>([&](Entity entity, const auto& region) {
            m_settings = region;
            center = Math::Vec3(scene.get_world_matrix(entity)[3]);
        });
        if(!m_geometry)
            return Result<void>::failure("Navigation requires a static collision geometry source");
        auto geometry =
            m_geometry(center - m_settings.half_extents, center + m_settings.half_extents);
        if(!geometry)
            return Result<void>::failure(geometry.error().message);
        return m_mesh.build(geometry.value(), center, m_settings);
    }
    Result<void, Error> NavigationSystem::fixed_update(Scene& scene, const Context& context) {
        // PhysicsSystem 已在 on_start 同步碰撞世界；首个固定步构建一次静态导航。
        if(!m_prepared) {
            if(auto result = prepare(scene); !result)
                return Result<void, Error>::failure({result.error()});
            m_prepared = true;
        }
        m_time = context.total_time;
        m_delta = float(context.delta_time);
        ++m_step;
        m_queries = 0;
        std::erase_if(m_agents, [&](const auto& entry) {
            const Entity entity = scene.find_entity(entry.first);
            return !entity || entity.get_id() != entry.second.entity
                   || !entity.has_component<NavigationAgentComponent>()
                   || entry.second.last_step + 1 < m_step;
        });
        return Result<void, Error>::success();
    }
    void NavigationSystem::on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept {
        m_agents.clear();
        m_mesh.clear();
        m_scene = nullptr;
        m_prepared = false;
        m_delta = 0;
    }
    Result<NavigationSystem::Status> NavigationSystem::move_to(
        Scene& scene, Entity entity, PhysicsAccess& physics, Math::Vec3 target, bool run) {
        using Output = Result<Status>;
        if(m_scene != &scene || !scene.is_valid(entity) || !physics.is_bound_to(scene)
            || m_delta <= 0)
            return Output::failure("Navigation movement requires the active fixed-step scene");
        if(!entity.has_component<CharacterControllerComponent>()
            || !entity.has_component<NavigationAgentComponent>())
            return Output::failure(
                "Navigation movement requires Character Controller and Navigation Agent");
        const auto& options = entity.get_component<NavigationAgentComponent>();
        const auto& character = entity.get_component<CharacterControllerComponent>();
        if(!Math::is_finite(target) || !std::isfinite(options.arrival_distance)
            || options.arrival_distance < 0.05f || !std::isfinite(options.repath_interval)
            || options.repath_interval < 0.05f)
            return Output::failure("Invalid navigation target or agent settings");
        const auto stop = [&](Status status) {
            if(character.enabled && !physics.request_character_move(entity, {}))
                return Output::failure("Cannot stop navigation character");
            return Output::success(status);
        };
        if(!options.enabled || !character.enabled)
            return stop(Status::Disabled);
        if(!m_mesh.ready())
            return Output::failure("Scene has no built Navigation Region");
        const auto scale = entity.get_component<TransformComponent>().scale.x;
        if(character.radius * scale > m_settings.agent_radius + 0.001f
            || character.height * scale > m_settings.agent_height + 0.001f
            || character.step_height * scale + 0.001f < m_settings.max_climb
            || character.max_slope_angle + 0.001f < m_settings.max_slope)
            return Output::failure(
                "Character dimensions or movement limits do not fit the Navigation Region");
        const auto position = Math::Vec3(scene.get_world_matrix(entity)[3]);
        auto& agent = m_agents[entity.get_uuid()];
        if(agent.last_step == m_step)
            return Output::success(agent.status);
        const bool query = agent.entity != entity.get_id() || m_time >= agent.next_query
                           || planar_distance(agent.target, target) > options.arrival_distance
                           || glm::distance(position, agent.previous_position) > 2;
        agent.entity = entity.get_id();
        agent.previous_position = position;
        agent.last_step = m_step;
        if(query) {
            // 每步最多八次完整查询；超预算不继续沿已失效目标行走。
            if(m_queries >= 8) {
                agent.status = Status::BudgetExceeded;
                return stop(agent.status);
            }
            ++m_queries;
            const auto path = m_mesh.find_path(position, target);
            if(!path)
                return Output::failure(path.error());
            agent.path = path.value();
            agent.target = target;
            agent.corner = 1;
            agent.next_query = m_time + options.repath_interval;
        }
        if(agent.path.status != NavigationMesh::Status::Complete) {
            agent.status = Status::NoPath;
            if(agent.path.status == NavigationMesh::Status::BudgetExceeded)
                agent.status = Status::BudgetExceeded;
            return stop(agent.status);
        }
        if(planar_distance(position, target) <= options.arrival_distance
            && std::abs(position.y - target.y) <= std::max(m_settings.max_climb, 0.2f))
            return stop(Status::Arrived);
        const float speed = run ? character.run_speed : character.move_speed;
        const float corner_distance = std::max(0.12f, speed * m_delta);
        while(agent.corner + 1 < agent.path.points.size()
              && planar_distance(position, agent.path.points[agent.corner]) < corner_distance)
            ++agent.corner;
        if(agent.corner >= agent.path.points.size())
            return stop(Status::NoPath);
        const auto destination = agent.path.points[agent.corner];
        const auto delta = Math::Vec2(destination.x - position.x, destination.z - position.z);
        const float distance = glm::length(delta);
        if(distance < 0.001f)
            return stop(Status::NoPath);
        const auto desired = delta / distance;
        Math::Vec2 direction{0};
        float best = 0;
        const float radius = character.radius * scale;
        const float horizon = std::max(radius * 2, speed * 0.35f);
        // 采样前方扇区，候选必须留在导航面上且通过物理扫掠。
        for(float angle : {0.f, 30.f, -30.f, 60.f, -60.f, 85.f, -85.f}) {
            const float radians = angle * Math::DEG_TO_RAD;
            const Math::Vec2 candidate{
                desired.x * std::cos(radians) - desired.y * std::sin(radians),
                desired.x * std::sin(radians) + desired.y * std::cos(radians)};
            const auto flat_step =
                Math::Vec3(candidate.x, 0, candidate.y) * std::min(horizon, distance);
            const auto surface = m_mesh.surface_step(position, position + flat_step);
            if(!surface)
                continue;
            const auto displacement = *surface - position;
            auto shape = ColliderComponent{};
            shape.shape = ColliderShape::Capsule;
            shape.radius = radius;
            shape.height = std::max(radius * 2, character.height * scale - m_settings.max_climb);
            auto pose = TransformComponent{};
            // 胶囊稍抬离地面，保留可跨越的台阶；Jolt 角色负责最终步进与接触。
            pose.translation =
                position + Math::Vec3(0, m_settings.max_climb + shape.height * 0.5f + 0.02f, 0);
            const auto hit = physics.shape_cast(shape, pose, displacement,
                {.ignored = entity.get_uuid(), .layers = character.collision_mask});
            if(!hit)
                return Output::failure(hit.error());
            const float clearance = hit.value() ? hit.value()->fraction : 1;
            const float score = clearance * (0.2f + 0.8f * glm::dot(candidate, desired));
            if(clearance > 0.2f && score > best) {
                best = score;
                direction = candidate * clearance;
            }
        }
        if(direction == Math::Vec2(0)) {
            agent.status = Status::Blocked;
            return stop(agent.status);
        }
        const float remaining = planar_distance(position, destination);
        if(speed > 0)
            direction *= std::min(1.f, remaining / (speed * m_delta));
        if(auto faced = CharacterController::face_movement(scene, options.facing, direction);
            !faced)
            return Output::failure(faced.error());
        if(!physics.request_character_move(entity, {.direction = direction, .run = run}))
            return Output::failure("Cannot submit navigation character movement");
        agent.status = Status::Moving;
        return Output::success(agent.status);
    }
    const char* NavigationSystem::name(Status status) {
        switch(status) {
            case Status::Moving:
                return "moving";
            case Status::Arrived:
                return "arrived";
            case Status::NoPath:
                return "no_path";
            case Status::Blocked:
                return "blocked";
            case Status::BudgetExceeded:
                return "budget_exceeded";
            case Status::Disabled:
                return "disabled";
        }
        return "disabled";
    }
}
