#include "gameplay/navigation_system.h"
#include "gameplay/character_controller.h"
#include "physics/physics_access.h"
#include "scene/scene.h"
#include <array>
#include "asset/data/navigation_data.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace Comet::Gameplay {
    namespace {
        float planar_distance(Math::Vec3 a, Math::Vec3 b) {
            return std::hypot(a.x - b.x, a.z - b.z);
        }
    }

    Result<std::vector<NavigationMesh::Link>> capture_navigation_links(Scene& scene) {
        using Output = Result<std::vector<NavigationMesh::Link>>;
        std::vector<std::pair<EntityUuid, NavigationMesh::Link>> ordered;
        scene.each<const NavigationLinkComponent>([&](Entity entity, const auto& settings) {
            const auto& world = scene.get_world_matrix(entity);
            ordered.emplace_back(
                entity.get_uuid(), NavigationMesh::Link{Math::Vec3(world[3]),
                                       Math::Vec3(world * Math::Vec4(settings.end, 1)),
                                       settings.radius, settings.bidirectional});
        });
        if(ordered.size() > 256)
            return Output::failure("最多支持 256 个导航连接");
        std::ranges::sort(ordered, {}, &decltype(ordered)::value_type::first);
        std::vector<NavigationMesh::Link> links;
        links.reserve(ordered.size());
        for(const auto& [uuid, link] : ordered) {
            if(!Math::is_finite(link.start) || !Math::is_finite(link.end)
                || !std::isfinite(link.radius) || link.radius <= 0 || link.radius > 10
                || Math::length(link.end - link.start) < .01f)
                return Output::failure("无效的导航连接端点或半径");
            links.push_back(link);
        }
        return Output::success(std::move(links));
    }
    NavigationSystem::NavigationSystem(GeometrySource geometry, ProductSource products)
        : m_geometry(std::move(geometry)), m_products(std::move(products)) {}
    Result<void, Error> NavigationSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices&) {
        m_scene = &scene;
        m_prepared = false;
        m_agents.clear();
        m_mesh.clear();
        m_step = 0;
        m_delta = 0;
        m_time = 0;
        m_statistics = {};
        m_links.clear();
        m_obstacles.clear();
        m_geometry_snapshot.clear();
        m_base_product.reset();
        m_mesh_generation = 0;
        return Result<void, Error>::success();
    }
    Result<void, Error> NavigationSystem::on_runtime_ready(
        Scene& scene, RuntimeSession&, const RuntimeServices&) {
        if(m_scene != &scene)
            return Result<void, Error>::failure({"Navigation requires its started scene"});
        if(m_prepared)
            return Result<void, Error>::success();
        // 注册顺序保留 Navigation -> Script -> Physics 的固定步意图提交顺序；
        // 此处全部 on_start 已完成，PhysicsSystem 已同步初始碰撞世界。
        if(scene.component_count<NavigationRegionComponent>() != 0) {
            const auto begin = std::chrono::steady_clock::now();
            const auto prepared = prepare(scene);
            m_statistics.prepare_milliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                    .count();
            if(!prepared)
                return Result<void, Error>::failure({prepared.error()});
        }
        m_prepared = true;
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
        m_center = center;
        auto links = capture_navigation_links(scene);
        if(!links)
            return Result<void>::failure(links.error());
        m_links = std::move(links).value();
        if(!m_geometry)
            return Result<void>::failure("Navigation requires a static collision geometry source");
        auto geometry =
            m_geometry(center - m_settings.half_extents, center + m_settings.half_extents);
        if(!geometry)
            return Result<void>::failure(geometry.error().message);
        if(m_settings.data) {
            const auto product = m_products ? m_products(m_settings.data) : nullptr;
            if(!product)
                return Result<void>::failure("Navigation product is not prepared");
            const auto fingerprint =
                NavigationMesh::input_fingerprint(geometry.value(), center, m_settings, m_links);
            if(!fingerprint)
                return Result<void>::failure(fingerprint.error());
            if(product->input_fingerprint != fingerprint.value())
                return Result<void>::failure(
                    "Navigation product is stale; rebake after changing geometry or settings");
            const auto begin = std::chrono::steady_clock::now();
            const auto loaded = m_mesh.load(*product);
            m_statistics.product_load_milliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                    .count();
            if(loaded)
                ++m_statistics.product_loads;
            if(!loaded)
                return loaded;
            m_base_product = product;
        } else {
            const auto begin = std::chrono::steady_clock::now();
            ++m_statistics.builds;
            const auto built = m_mesh.build(geometry.value(), center, m_settings, m_links);
            m_statistics.build_milliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                    .count();
            if(!built)
                return built;
        }
        ++m_mesh_generation;
        if(scene.component_count<NavigationObstacleComponent>() > 0)
            m_geometry_snapshot = std::move(geometry).value();
        return synchronize_obstacles(scene);
    }

    Result<void> NavigationSystem::synchronize_obstacles(Scene& scene) {
        if(!m_mesh.ready())
            return Result<void>::success();
        std::vector<NavigationMesh::Obstacle> next;
        auto valid = Result<void>::success();
        scene.each<const NavigationObstacleComponent>([&](Entity entity, const auto& obstacle) {
            if(!valid || !obstacle.enabled)
                return;
            if(!Math::is_finite(obstacle.half_extents)
                || !glm::all(glm::greaterThan(obstacle.half_extents, Math::Vec3(0)))) {
                valid = Result<void>::failure("导航障碍半尺寸必须为正数");
                return;
            }
            if(entity.has_component<RigidBodyComponent>()
                && entity.get_component<RigidBodyComponent>().motion == BodyMotion::Static) {
                valid = Result<void>::failure(
                    "动态导航障碍应使用运动学刚体；静态碰撞体已包含在烘焙几何中");
                return;
            }
            const auto& world = scene.get_world_matrix(entity);
            const auto center = Math::Vec3(world[3]);
            Math::Vec3 extent{0};
            for(int axis = 0; axis < 3; ++axis)
                extent += glm::abs(Math::Vec3(world[axis])) * obstacle.half_extents[axis];
            if(glm::any(glm::lessThan(center + extent, m_center - m_settings.half_extents))
                || glm::any(glm::greaterThan(center - extent, m_center + m_settings.half_extents)))
                return;
            auto minimum = center - extent;
            auto maximum = center + extent;
            // 与 Recast 栅格对齐：同一批被遮挡单元内的微小移动不触发重建。
            const auto origin = m_center - m_settings.half_extents;
            for(int axis = 0; axis < 3; ++axis) {
                const float cell =
                    axis == 1 ? std::min(.1f, m_settings.cell_size * .5f) : m_settings.cell_size;
                minimum[axis] =
                    origin[axis] + std::floor((minimum[axis] - origin[axis]) / cell) * cell;
                maximum[axis] =
                    origin[axis] + std::ceil((maximum[axis] - origin[axis]) / cell) * cell;
            }
            next.push_back({minimum, maximum});
        });
        if(!valid)
            return valid;
        if(next.size() > 128)
            return Result<void>::failure("最多支持 128 个动态导航障碍");
        const auto key = [](const auto& box) {
            return std::array{box.minimum.x, box.minimum.y, box.minimum.z, box.maximum.x,
                box.maximum.y, box.maximum.z};
        };
        std::ranges::sort(next, {}, key);
        if(next == m_obstacles)
            return Result<void>::success();
        if(m_geometry_snapshot.empty()) {
            auto geometry =
                m_geometry(m_center - m_settings.half_extents, m_center + m_settings.half_extents);
            if(!geometry)
                return Result<void>::failure(geometry.error().message);
            m_geometry_snapshot = std::move(geometry).value();
        }
        const auto begin = std::chrono::steady_clock::now();
        const auto built =
            next.empty() && m_base_product
                ? m_mesh.load(*m_base_product)
                : m_mesh.build(m_geometry_snapshot, m_center, m_settings, m_links, next);
        m_statistics.obstacle_rebuild_milliseconds +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                .count();
        if(!built)
            return built;
        ++m_statistics.obstacle_rebuilds;
        ++m_mesh_generation;
        m_obstacles = std::move(next);
        return Result<void>::success();
    }
    Result<void, Error> NavigationSystem::fixed_update(Scene& scene, const Context& context) {
        if(m_scene != &scene || !m_prepared)
            return Result<void, Error>::failure(
                {"Navigation startup preparation has not completed"});
        m_time = context.total_time;
        m_delta = float(context.delta_time);
        ++m_step;
        m_statistics.step_path_queries = 0;
        if(auto obstacles = synchronize_obstacles(scene); !obstacles)
            return Result<void, Error>::failure({obstacles.error()});
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
        m_geometry_snapshot.clear();
        m_links.clear();
        m_obstacles.clear();
        m_base_product.reset();
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
            if(auto found = m_agents.find(entity.get_uuid()); found != m_agents.end()) {
                found->second.status = status;
                found->second.jump.reset();
            }
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
        if(agent.jump) {
            const auto state = physics.get_character_state(entity);
            const auto distance = planar_distance(position, agent.jump->end);
            if(m_time >= agent.jump->deadline - 2 * m_delta && state && state->grounded) {
                if(distance > std::max(options.arrival_distance, .5f))
                    return stop(Status::Blocked);
                agent.jump.reset();
                ++agent.corner;
                agent.next_query = 0;
            } else {
                if(m_time > agent.jump->deadline + 1) {
                    agent.jump.reset();
                    agent.status = Status::Blocked;
                    return stop(agent.status);
                }
                auto velocity = agent.jump->velocity;
                if(distance < Math::length(velocity) * m_delta)
                    velocity *= distance / std::max(Math::length(velocity) * m_delta, .001f);
                if(!physics.request_character_move(entity, {})
                    || !physics.request_character_velocity(
                        entity, {.velocity = velocity, .horizontal = true, .vertical = false}))
                    return Output::failure("Cannot continue navigation jump");
                return Output::success(Status::Moving);
            }
        }
        if(query || agent.mesh_generation != m_mesh_generation) {
            // 每步最多八次完整查询；超预算不继续沿已失效目标行走。
            if(m_statistics.step_path_queries >= 8) {
                agent.status = Status::BudgetExceeded;
                return stop(agent.status);
            }
            ++m_statistics.step_path_queries;
            ++m_statistics.path_queries;
            const auto path = m_mesh.find_path(position, target);
            if(!path)
                return Output::failure(path.error());
            agent.path = path.value();
            agent.target = target;
            agent.corner = 1;
            if(std::ranges::any_of(
                   agent.path.jumps, [](const auto& jump) { return jump.corner == 0; }))
                agent.corner = 0;
            agent.next_query = m_time + options.repath_interval;
            agent.mesh_generation = m_mesh_generation;
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
        const auto jump_at = [&](std::size_t corner) -> const NavigationMesh::Path::Jump* {
            for(const auto& jump : agent.path.jumps)
                if(jump.corner == corner)
                    return &jump;
            return nullptr;
        };
        while(agent.corner + 1 < agent.path.points.size()
              && planar_distance(position, agent.path.points[agent.corner]) < corner_distance
              && !jump_at(agent.corner))
            ++agent.corner;
        if(agent.corner >= agent.path.points.size())
            return stop(Status::NoPath);
        const auto destination = agent.path.points[agent.corner];
        if(const auto* jump = jump_at(agent.corner);
            jump
            && planar_distance(position, destination) <= std::min(corner_distance, jump->radius)) {
            const auto state = physics.get_character_state(entity);
            const float gravity = 9.81f * character.gravity_scale;
            const float vertical = character.jump_speed;
            const float discriminant =
                vertical * vertical - 2 * gravity * (jump->end.y - position.y);
            if(!state || !state->grounded || gravity <= 0 || discriminant < 0)
                return stop(Status::Blocked);
            const float duration = (vertical + std::sqrt(discriminant)) / gravity;
            auto velocity = (jump->end - position) / duration;
            velocity.y = 0;
            if(!Math::is_finite(velocity) || Math::length(velocity) > speed + .01f)
                return stop(Status::Blocked);
            agent.jump = Agent::Jump{jump->end, velocity, m_time + duration};
            velocity.y = vertical;
            if(!physics.request_character_move(entity, {})
                || !physics.request_character_velocity(
                    entity, {.velocity = velocity, .horizontal = true, .vertical = true}))
                return Output::failure("Cannot start navigation jump");
            agent.status = Status::Moving;
            return Output::success(agent.status);
        }
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
            ++m_statistics.surface_queries;
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
            ++m_statistics.obstacle_queries;
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
