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
        constexpr unsigned PATH_QUERY_BUDGET = 8;

        float planar_distance(Math::Vec3 a, Math::Vec3 b) {
            return std::hypot(a.x - b.x, a.z - b.z);
        }

        std::optional<NavigationMesh::Obstacle> quantized_obstacle(const Math::Mat4& world,
            Math::Vec3 half_extents, Math::Vec3 region_center,
            const NavigationRegionComponent& settings) {
            const auto center = Math::Vec3(world[3]);
            Math::Vec3 extent{0};
            for(int axis = 0; axis < 3; ++axis)
                extent += Math::abs(Math::Vec3(world[axis])) * half_extents[axis];
            NavigationMesh::Obstacle box{center - extent, center + extent};
            const auto origin = region_center - settings.half_extents;
            if(Math::any_less(box.maximum, origin)
                || Math::any_greater(box.minimum, region_center + settings.half_extents))
                return std::nullopt;
            // 与 Recast 栅格对齐：同一批被遮挡单元内的微小移动不触发重建。
            for(int axis = 0; axis < 3; ++axis) {
                const float cell =
                    axis == 1 ? std::min(.1f, settings.cell_size * .5f) : settings.cell_size;
                box.minimum[axis] =
                    origin[axis] + std::floor((box.minimum[axis] - origin[axis]) / cell) * cell;
                box.maximum[axis] =
                    origin[axis] + std::ceil((box.maximum[axis] - origin[axis]) / cell) * cell;
            }
            return box;
        }

        struct JumpTrajectory {
            Math::Vec3 velocity;
            float duration;
        };
        std::optional<JumpTrajectory> jump_trajectory(Math::Vec3 start, Math::Vec3 end,
            const CharacterControllerComponent& character, float speed) {
            const float gravity = 9.81f * character.gravity_scale;
            const float vertical = character.jump_speed;
            const float discriminant = vertical * vertical - 2 * gravity * (end.y - start.y);
            if(gravity <= 0 || discriminant < 0)
                return std::nullopt;
            const float duration = (vertical + std::sqrt(discriminant)) / gravity;
            auto velocity = (end - start) / duration;
            velocity.y = 0;
            if(!Math::is_finite(velocity) || Math::length(velocity) > speed + .01f)
                return std::nullopt;
            return JumpTrajectory{velocity, duration};
        }

        bool submit_jump(
            PhysicsAccess& physics, Entity entity, Math::Vec3 velocity, bool takeoff = false) {
            return physics.request_character_move(entity, {})
                   && physics.request_character_velocity(
                       entity, {.velocity = velocity, .horizontal = true, .vertical = takeoff});
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
        m_pending_queries.clear();
        m_reserved_queries = 0;
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
                || !Math::all_greater(obstacle.half_extents, Math::Vec3(0))) {
                valid = Result<void>::failure("导航障碍半尺寸必须为正数");
                return;
            }
            if(entity.has_component<RigidBodyComponent>()
                && entity.get_component<RigidBodyComponent>().motion == BodyMotion::Static) {
                valid = Result<void>::failure(
                    "动态导航障碍应使用运动学刚体；静态碰撞体已包含在烘焙几何中");
                return;
            }
            if(const auto box = quantized_obstacle(
                   scene.get_world_matrix(entity), obstacle.half_extents, m_center, m_settings))
                next.push_back(*box);
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
        auto built = Result<void>::success();
        if(next.empty() && m_base_product)
            built = m_mesh.load(*m_base_product);
        else
            built = m_mesh.build(m_geometry_snapshot, m_center, m_settings, m_links, next);
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
        std::erase_if(m_pending_queries, [&](EntityUuid uuid) { return !m_agents.contains(uuid); });
        m_reserved_queries = 0;
        for(auto& [uuid, agent] : m_agents)
            agent.query_reserved = false;
        // 先为上一步未获预算的请求预留位置，调用顺序不再决定长期查询优先级。
        for(const auto uuid : m_pending_queries) {
            if(m_reserved_queries == PATH_QUERY_BUDGET)
                break;
            m_agents.at(uuid).query_reserved = true;
            ++m_reserved_queries;
        }
        return Result<void, Error>::success();
    }
    void NavigationSystem::on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept {
        m_agents.clear();
        m_pending_queries.clear();
        m_reserved_queries = 0;
        m_mesh.clear();
        m_scene = nullptr;
        m_prepared = false;
        m_delta = 0;
        m_geometry_snapshot.clear();
        m_links.clear();
        m_obstacles.clear();
        m_base_product.reset();
    }
    bool NavigationSystem::is_traversing_link(const Scene& scene, Entity entity) const {
        if(m_scene != &scene || !scene.is_valid(entity))
            return false;
        const auto found = m_agents.find(entity.get_uuid());
        return found != m_agents.end() && found->second.entity == entity.get_id()
               && found->second.last_step == m_step && found->second.jump.has_value();
    }
    void NavigationSystem::cancel_path_query(EntityUuid uuid, Agent& agent) {
        if(agent.query_reserved)
            --m_reserved_queries;
        if(agent.query_pending)
            std::erase(m_pending_queries, uuid);
        agent.query_reserved = false;
        agent.query_pending = false;
    }
    bool NavigationSystem::acquire_path_query(EntityUuid uuid, Agent& agent) {
        if(!agent.query_reserved
            && m_statistics.step_path_queries + m_reserved_queries >= PATH_QUERY_BUDGET) {
            if(!agent.query_pending) {
                agent.query_pending = true;
                m_pending_queries.push_back(uuid);
            }
            return false;
        }
        cancel_path_query(uuid, agent);
        ++m_statistics.step_path_queries;
        ++m_statistics.path_queries;
        return true;
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
                if(status != Status::BudgetExceeded)
                    cancel_path_query(entity.get_uuid(), found->second);
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
                           || Math::distance(position, agent.previous_position) > 2;
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
                if(m_time > agent.jump->deadline + 1)
                    return stop(Status::Blocked);
                auto velocity = agent.jump->velocity;
                if(distance < Math::length(velocity) * m_delta)
                    velocity *= distance / std::max(Math::length(velocity) * m_delta, .001f);
                if(!submit_jump(physics, entity, velocity))
                    return Output::failure("Cannot continue navigation jump");
                return Output::success(Status::Moving);
            }
        }
        if(query || agent.mesh_generation != m_mesh_generation) {
            // 超预算不继续沿已失效目标行走；待查请求保留到下一步。
            if(!acquire_path_query(entity.get_uuid(), agent))
                return stop(Status::BudgetExceeded);
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
        } else {
            cancel_path_query(entity.get_uuid(), agent);
        }
        if(agent.path.status == NavigationMesh::Status::BudgetExceeded)
            return stop(Status::BudgetExceeded);
        if(agent.path.status != NavigationMesh::Status::Complete)
            return stop(Status::NoPath);
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
            const auto trajectory = jump_trajectory(position, jump->end, character, speed);
            if(!state || !state->grounded || !trajectory)
                return stop(Status::Blocked);
            agent.jump =
                Agent::Jump{jump->end, trajectory->velocity, m_time + trajectory->duration};
            auto velocity = trajectory->velocity;
            velocity.y = character.jump_speed;
            if(!submit_jump(physics, entity, velocity, true))
                return Output::failure("Cannot start navigation jump");
            agent.status = Status::Moving;
            return Output::success(agent.status);
        }
        if(planar_distance(position, destination) < .001f)
            return stop(Status::NoPath);
        const auto steering = steer(entity, physics, position, destination, speed, scale);
        if(!steering)
            return Output::failure(steering.error());
        const auto direction = steering.value();
        if(direction == Math::Vec2(0))
            return stop(Status::Blocked);
        if(auto faced = CharacterController::face_movement(scene, options.facing, direction);
            !faced)
            return Output::failure(faced.error());
        if(!physics.request_character_move(entity, {.direction = direction, .run = run}))
            return Output::failure("Cannot submit navigation character movement");
        agent.status = Status::Moving;
        return Output::success(agent.status);
    }
    Result<Math::Vec2> NavigationSystem::steer(Entity entity, PhysicsAccess& physics,
        Math::Vec3 position, Math::Vec3 destination, float speed, float scale) {
        const auto& character = entity.get_component<CharacterControllerComponent>();
        const auto delta = Math::Vec2(destination.x - position.x, destination.z - position.z);
        const float distance = Math::length(delta);
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
                return Result<Math::Vec2>::failure(hit.error());
            const float clearance = hit.value() ? hit.value()->fraction : 1;
            const float score = clearance * (0.2f + 0.8f * Math::dot(candidate, desired));
            if(clearance > 0.2f && score > best) {
                best = score;
                direction = candidate * clearance;
            }
        }
        if(speed > 0)
            direction *= std::min(1.f, distance / (speed * m_delta));
        return Result<Math::Vec2>::success(direction);
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
