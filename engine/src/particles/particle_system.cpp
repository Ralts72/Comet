#include "particles/particle_system.h"

#include "scene/scene.h"

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

    void ParticleSystem::advance(Particle& particle, const Emitter& emitter, const double delta) {
        const auto dt = float(delta);
        particle.position +=
            particle.velocity * dt + emitter.settings.acceleration * (0.5f * dt * dt);
        particle.velocity += emitter.settings.acceleration * dt;
        particle.age += delta;
    }

    void ParticleSystem::emit(
        Emitter& emitter, uint32_t count, const double delta, const double previous) {
        const auto& settings = emitter.settings;
        if(emitter.particles.size() >= settings.capacity
            || m_count >= ParticleSnapshot::MAX_PARTICLES)
            return;
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
                advance(particle, emitter, age);
            }
            if(particle.age < settings.lifetime) {
                emitter.particles.push_back(particle);
                ++m_count;
            }
        }
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
            emit(emitter, emitter.settings.burst);
        }
        return result;
    }

    Result<void, Error> ParticleSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices&) {
        m_emitters.clear();
        m_count = 0;
        if(auto synced = synchronize(scene); !synced)
            return synced;
        publish(scene);
        return Result<void, Error>::success();
    }

    Result<void, Error> ParticleSystem::fixed_update(Scene& scene, const Context& context) {
        if(auto synced = synchronize(scene); !synced)
            return synced;
        for(auto& [id, emitter] : m_emitters) {
            for(auto& particle : emitter.particles)
                advance(particle, emitter, context.delta_time);
            const auto removed = std::erase_if(emitter.particles,
                [&](const auto& particle) { return particle.age >= emitter.settings.lifetime; });
            m_count -= uint32_t(removed);
            if(!emitter.present || !emitter.settings.enabled || emitter.settings.rate == 0)
                continue;
            const auto previous = emitter.remainder;
            const auto total = previous + emitter.settings.rate * context.delta_time;
            const auto count = uint32_t(std::floor(total));
            emitter.remainder = total - count;
            emit(emitter, count, context.delta_time, previous);
        }
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
            emit(emitter, request.count);
        }
        std::erase_if(m_emitters, [](const auto& entry) {
            return !entry.second.present && entry.second.particles.empty();
        });
        publish(scene);
        return Result<void, Error>::success();
    }

    void ParticleSystem::publish(Scene& scene) {
        if(m_count == 0) {
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
        scene.set_particles(std::move(snapshot));
    }

    void ParticleSystem::on_stop(Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept {
        m_emitters.clear();
        m_count = 0;
        scene.set_particles(nullptr);
    }
}
