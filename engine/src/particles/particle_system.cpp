#include "particles/particle_system.h"

#include "scene/scene.h"
#include "physics/physics_access.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Comet {
    float ParticleSystem::random_unit(Emitter& emitter) {
        auto& value = emitter.random;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        return float(value >> 8) / 16777216.0f;
    }

    Result<void, Error> ParticleSystem::advance(
        Particle& particle, const Emitter& emitter, const double delta) {
        const auto& settings = emitter.settings;
        float remaining = float(delta);
        // 有限次扫掠覆盖一次步进内的反弹；达到上限时停在安全位置，不穿过下一面墙。
        for(unsigned iteration = 0; iteration < 3 && remaining > 0; ++iteration) {
            const auto movement = particle.velocity * remaining
                                  + settings.acceleration * (0.5f * remaining * remaining);
            std::optional<PhysicsAccess::QueryHit> hit;
            if(settings.collision != ParticleCollision::None
                && Math::dot(movement, movement) > 1e-12f) {
                if(!m_physics)
                    return Result<void, Error>::failure({"粒子碰撞需要运行域物理服务"});
                const PhysicsAccess::QueryFilter filter{
                    .ignored = emitter.ignored, .layers = settings.collision_layers};
                const auto query = [&] {
                    if(settings.collision_radius == 0)
                        return m_physics->raycast(particle.position, movement, filter);
                    ColliderComponent sphere;
                    sphere.shape = ColliderShape::Sphere;
                    sphere.radius = settings.collision_radius;
                    return m_physics->shape_cast(sphere,
                        TransformComponent{.translation = particle.position}, movement, filter);
                };
                auto queried = query();
                if(!queried)
                    return Result<void, Error>::failure({queried.error()});
                hit = queried.value();
            }
            const auto fraction = hit ? hit->fraction : 1.0f;
            const auto elapsed = remaining * fraction;
            particle.position += movement * fraction;
            particle.velocity += settings.acceleration * elapsed;
            if(!hit)
                break;
            if(settings.collision == ParticleCollision::Kill) {
                particle.age = settings.lifetime;
                return Result<void, Error>::success();
            }
            const auto normal_speed = Math::dot(particle.velocity, hit->normal);
            if(normal_speed < 0) {
                const auto tangent = particle.velocity - hit->normal * normal_speed;
                particle.velocity = tangent * (1 - settings.friction)
                                    - hit->normal * normal_speed * settings.restitution;
            }
            particle.position += hit->normal * 0.001f;
            remaining -= elapsed;
        }
        particle.age += delta;
        return Result<void, Error>::success();
    }

    Result<void, Error> ParticleSystem::emit(
        Emitter& emitter, uint32_t count, const double delta, const double previous) {
        const auto& settings = emitter.settings;
        if(emitter.particles.size() >= settings.capacity
            || m_count >= ParticleSnapshot::MAX_PARTICLES)
            return Result<void, Error>::success();
        const auto available = std::min(settings.capacity - uint32_t(emitter.particles.size()),
            ParticleSnapshot::MAX_PARTICLES - m_count);
        count = std::min(count, available);
        const float cosine = std::cos(settings.spread * Math::PI / 180);
        for(uint32_t index = 0; index < count; ++index) {
            const float y = cosine + (1 - cosine) * random_unit(emitter);
            const float radius = std::sqrt(std::max(0.0f, 1 - y * y));
            const float angle = 2 * Math::PI * random_unit(emitter);
            const Math::Vec3 direction{radius * std::cos(angle), y, radius * std::sin(angle)};
            Particle particle{Math::Vec3(emitter.pose[3]),
                Math::Vec3(emitter.pose * Math::Vec4(direction, 0)) * settings.speed};
            if(delta > 0) {
                // 连续发射保留本固定步内的出生时间，而不是整批在步末出生。
                const double age = std::max(0.0, delta - (index + 1 - previous) / settings.rate);
                if(auto advanced = advance(particle, emitter, age); !advanced)
                    return advanced;
            }
            if(particle.age < settings.lifetime) {
                emitter.particles.push_back(particle);
                ++m_count;
            }
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> ParticleSystem::synchronize(Scene& scene) {
        for(auto& [id, emitter] : m_emitters)
            emitter.present = false;
        scene.update_world_transforms();
        auto result = Result<void, Error>::success();
        std::vector<EntityId> initial_bursts;
        scene.each<const ParticleEmitterComponent>([&](Entity entity, const auto& settings) {
            if(!result)
                return;
            if(auto valid = settings.validate(); !valid) {
                result = Result<void, Error>::failure({valid.error()});
                return;
            }
            auto [found, inserted] = m_emitters.try_emplace(entity.get_id());
            auto& emitter = found->second;
            if(inserted || emitter.settings.seed != settings.seed)
                emitter.random = settings.seed ? settings.seed : 1;
            emitter.present = true;
            emitter.ignored = entity.get_uuid();
            emitter.settings = settings;
            const auto& world = entity.get_component<WorldTransformComponent>();
            emitter.pose = world.pose_world_matrix;
            emitter.pose[3] = world.world_matrix[3];
            if(emitter.particles.size() > settings.capacity) {
                m_count -= uint32_t(emitter.particles.size() - settings.capacity);
                emitter.particles.resize(settings.capacity);
            }
            if(inserted && settings.enabled && settings.burst > 0)
                initial_bursts.push_back(entity.get_id());
        });
        if(!result)
            return result;
        std::ranges::sort(initial_bursts);
        for(const auto id : initial_bursts) {
            auto& emitter = m_emitters.at(id);
            if(auto emitted = emit(emitter, emitter.settings.burst); !emitted)
                return emitted;
        }
        for(auto& [id, trail] : m_trails)
            trail.present = false;
        scene.each<const TrailComponent>([&](Entity entity, const auto& settings) {
            if(!result)
                return;
            if(auto valid = settings.validate(); !valid) {
                result = Result<void, Error>::failure({valid.error()});
                return;
            }
            auto& trail = m_trails[entity.get_id()];
            trail.present = true;
            trail.settings = settings;
        });
        return result;
    }

    Result<void, Error> ParticleSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices& services) {
        m_emitters.clear();
        m_trails.clear();
        m_physics = services.physics;
        m_time = 0;
        m_count = 0;
        if(auto synced = synchronize(scene); !synced)
            return synced;
        advance_trails(scene);
        publish(scene);
        return Result<void, Error>::success();
    }

    Result<void, Error> ParticleSystem::fixed_update(Scene& scene, const Context& context) {
        if(auto synced = synchronize(scene); !synced)
            return synced;
        m_time += context.delta_time;
        for(auto& [id, emitter] : m_emitters) {
            for(auto& particle : emitter.particles)
                if(auto advanced = advance(particle, emitter, context.delta_time); !advanced)
                    return advanced;
            const auto removed = std::erase_if(emitter.particles,
                [&](const auto& particle) { return particle.age >= emitter.settings.lifetime; });
            m_count -= uint32_t(removed);
            if(!emitter.present || !emitter.settings.enabled || emitter.settings.rate == 0)
                continue;
            const auto previous = emitter.remainder;
            const auto total = previous + emitter.settings.rate * context.delta_time;
            const auto count = uint32_t(std::floor(total));
            emitter.remainder = total - count;
            if(auto emitted = emit(emitter, count, context.delta_time, previous); !emitted)
                return emitted;
        }
        advance_trails(scene);
        return Result<void, Error>::success();
    }

    Result<void, Error> ParticleSystem::post_update(Scene& scene, const Context&) {
        if(auto synced = synchronize(scene); !synced)
            return synced;
        for(const auto& request : std::exchange(scene.m_particle_requests, {})) {
            auto [found, inserted] = m_emitters.try_emplace(request.entity);
            auto& emitter = found->second;
            if(request.clear) {
                m_count -= uint32_t(emitter.particles.size());
                emitter.particles.clear();
                emitter.remainder = 0;
                continue;
            }
            if(inserted || emitter.settings.seed != request.settings.seed)
                emitter.random = request.settings.seed ? request.settings.seed : 1;
            emitter.settings = request.settings;
            emitter.pose = request.pose;
            if(auto emitted = emit(emitter, request.count); !emitted)
                return emitted;
        }
        std::erase_if(m_emitters, [](const auto& entry) {
            return !entry.second.present && entry.second.particles.empty();
        });
        publish(scene);
        return Result<void, Error>::success();
    }

    void ParticleSystem::advance_trails(Scene& scene) {
        for(auto& [id, trail] : m_trails) {
            auto& points = trail.points;
            std::erase_if(points,
                [&](const auto& point) { return m_time - point.time >= trail.settings.lifetime; });
            const bool emitting = trail.present && trail.settings.enabled;
            if(emitting) {
                const auto entity = scene.find_entity(id);
                const auto position = Math::Vec3(scene.get_world_matrix(entity)[3]);
                const float distance =
                    points.empty() ? 0 : Math::length(position - points.back().position);
                if(points.empty() || !trail.emitting
                    || distance >= trail.settings.minimum_distance) {
                    const bool connected = !points.empty() && trail.emitting
                                           && distance <= trail.settings.break_distance;
                    points.push_back({position, m_time, connected});
                } else
                    points.back().time = m_time;
            }
            trail.emitting = emitting;
            if(points.size() > trail.settings.capacity)
                points.erase(points.begin(), points.end() - trail.settings.capacity);
        }
        std::erase_if(m_trails,
            [](const auto& entry) { return !entry.second.present && entry.second.points.empty(); });
    }

    void ParticleSystem::publish(Scene& scene) {
        if(m_count == 0 && m_trails.empty()) {
            scene.set_particles(nullptr);
            return;
        }
        auto snapshot = std::make_shared<ParticleSnapshot>();
        snapshot->instances.reserve(m_count);
        std::vector<const Emitter*> emitters;
        emitters.reserve(m_emitters.size());
        for(const auto& [id, emitter] : m_emitters)
            if(!emitter.particles.empty())
                emitters.push_back(&emitter);
        // 快照按状态分组；Alpha 的相机相关顺序由 Render 生成。
        std::stable_sort(emitters.begin(), emitters.end(), [](const auto* a, const auto* b) {
            if(a->settings.blend != b->settings.blend)
                return a->settings.blend < b->settings.blend;
            if(a->settings.texture != b->settings.texture)
                return a->settings.texture < b->settings.texture;
            return a->settings.soft_distance < b->settings.soft_distance;
        });
        for(const auto* source : emitters) {
            const auto& emitter = *source;
            const auto& settings = emitter.settings;
            const auto count = uint32_t(emitter.particles.size());
            if(!snapshot->batches.empty() && snapshot->batches.back().texture == settings.texture
                && snapshot->batches.back().blend == settings.blend
                && snapshot->batches.back().soft_distance == settings.soft_distance)
                snapshot->batches.back().count += count;
            else
                snapshot->batches.push_back({settings.texture, uint32_t(snapshot->instances.size()),
                    count, settings.blend, settings.soft_distance});
            for(const auto& particle : emitter.particles) {
                const float phase = std::clamp(float(particle.age / settings.lifetime), 0.0f, 1.0f);
                snapshot->instances.push_back(
                    {Math::Vec4(particle.position,
                         settings.start_size + (settings.end_size - settings.start_size) * phase),
                        settings.start_color + (settings.end_color - settings.start_color) * phase,
                        settings.texture_region(particle.age)});
            }
        }
        for(const auto& [id, trail] : m_trails) {
            const auto& settings = trail.settings;
            const auto first = uint32_t(snapshot->instances.size());
            for(std::size_t i = 1; i < trail.points.size()
                                   && snapshot->instances.size() < ParticleSnapshot::MAX_PARTICLES;
                ++i) {
                const auto& a = trail.points[i - 1];
                const auto& b = trail.points[i];
                if(!b.connected)
                    continue;
                const auto strength = [&](const auto& point) {
                    return std::clamp(
                        1.0f - float((m_time - point.time) / settings.lifetime), 0.0f, 1.0f);
                };
                const float head = strength(a), tail = strength(b);
                auto color = settings.color;
                color.w *= (head + tail) * 0.5f;
                snapshot->instances.push_back({Math::Vec4(a.position, settings.width * head), color,
                    {0, 0, 1, 1}, Math::Vec4(b.position, settings.width * tail)});
            }
            const auto count = uint32_t(snapshot->instances.size()) - first;
            if(count)
                snapshot->batches.push_back(
                    {settings.texture, first, count, settings.blend, settings.soft_distance});
        }
        if(snapshot->instances.empty())
            scene.set_particles(nullptr);
        else
            scene.set_particles(std::move(snapshot));
    }

    void ParticleSystem::on_stop(Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept {
        m_emitters.clear();
        m_trails.clear();
        m_physics = nullptr;
        m_count = 0;
        scene.set_particles(nullptr);
    }
}
