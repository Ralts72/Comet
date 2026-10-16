#include "inspector/particle_preview.h"

#include "particles/particle_system.h"
#include "scene/scene.h"
#include "scene/systems/physics_system.h"

#include <algorithm>
#include <glm/gtc/quaternion.hpp>

namespace CometEditor {
    ParticlePreview::ParticlePreview(Comet::PhysicsService::MeshSource meshes)
        : m_physics(std::make_unique<Comet::PhysicsService>(std::move(meshes))) {}
    ParticlePreview::~ParticlePreview() = default;

    Comet::Result<void, Comet::Error> ParticlePreview::bind(Target target,
        const Comet::ParticleEmitterComponent& settings, const Comet::Math::Mat4& pose,
        Comet::Scene* collision_scene, const Comet::TrailComponent* trail) {
        using Result = Comet::Result<void, Comet::Error>;
        if(target.scene_lifetime == 0 || !target.entity)
            return Result::failure({"Particle preview requires a valid scene and entity"});
        if(auto valid = settings.validate(); !valid)
            return Result::failure({valid.error()});
        if(trail)
            if(auto valid = trail->validate(); !valid)
                return Result::failure({valid.error()});
        if(m_target != target) {
            reset();
            m_world = std::make_unique<Comet::Scene>();
            auto entity = m_world->create_entity_with_uuid(target.entity, "Particle Preview");
            entity.add_component<Comet::ParticleEmitterComponent>(settings);
            if(auto services = m_runtime.set_services({.physics = m_physics.get()}); !services)
                return services;
            if(auto added =
                    m_runtime.add_system(std::make_unique<Comet::PhysicsSystem>(*m_physics));
                !added)
                return added;
            if(auto added = m_runtime.add_system(std::make_unique<Comet::ParticleSystem>()); !added)
                return added;
            m_target = target;
        }
        if(collision_scene && settings.collision != Comet::ParticleCollision::None)
            synchronize_colliders(*collision_scene);
        auto entity = m_world->find_entity(target.entity);
        bool trail_changed = trail != nullptr && !entity.has_component<Comet::TrailComponent>();
        if(trail) {
            if(!entity.has_component<Comet::TrailComponent>())
                entity.add_component<Comet::TrailComponent>(*trail);
            else {
                trail_changed = entity.get_component<Comet::TrailComponent>() != *trail;
                if(!entity.try_set_component(*trail))
                    return Result::failure({"Invalid trail preview settings"});
            }
        } else if(entity.has_component<Comet::TrailComponent>()) {
            entity.remove_component<Comet::TrailComponent>();
            trail_changed = true;
        }
        const Comet::TransformComponent transform{.translation = Comet::Math::Vec3(pose[3]),
            .rotation = glm::degrees(glm::eulerAngles(glm::quat_cast(Comet::Math::Mat3(pose))))};
        const auto& before = entity.get_component<Comet::TransformComponent>();
        const bool changed =
            trail_changed || settings != entity.get_component<Comet::ParticleEmitterComponent>()
            || before.translation != transform.translation || before.rotation != transform.rotation;
        if(!entity.try_set_component(settings) || !entity.try_set_transform(transform))
            return Result::failure({"Invalid particle preview transform"});
        if(changed && m_runtime.is_active())
            return flush();
        return Result::success();
    }

    void ParticlePreview::synchronize_colliders(Comet::Scene& source) {
        std::vector<Comet::EntityUuid> current;
        source.each<const Comet::ColliderComponent, const Comet::RigidBodyComponent>(
            [&](Comet::Entity entity, const auto& collider, const auto&) {
                const auto uuid = entity.get_uuid();
                if(uuid == m_target.entity)
                    return;
                current.push_back(uuid);
                auto copy = m_world->find_entity(uuid);
                if(!copy) {
                    copy = m_world->create_entity_with_uuid(uuid, "Preview Collider");
                    copy.add_component<Comet::RigidBodyComponent>(
                        Comet::RigidBodyComponent{.motion = Comet::BodyMotion::Static});
                    copy.add_component<Comet::ColliderComponent>(collider);
                } else
                    static_cast<void>(copy.try_set_component(collider));
                copy.set_transform(entity.get_component<Comet::TransformComponent>());
            });
        std::ranges::sort(current);
        for(const auto uuid : m_collision_entities)
            if(!std::ranges::binary_search(current, uuid))
                m_world->destroy_entity(m_world->find_entity(uuid));
        m_collision_entities = std::move(current);
    }

    Comet::Result<void, Comet::Error> ParticlePreview::play() {
        if(!m_world)
            return Comet::Result<void, Comet::Error>::failure(
                {"Select a particle emitter before previewing"});
        if(!m_runtime.is_active())
            return m_runtime.start(*m_world);
        return m_runtime.set_state(Comet::SceneRuntime::State::Running);
    }

    Comet::Result<void, Comet::Error> ParticlePreview::flush() {
        const auto state = m_runtime.get_state();
        if(auto resumed = m_runtime.set_state(Comet::SceneRuntime::State::Running); !resumed)
            return resumed;
        if(auto advanced = m_runtime.advance(0); !advanced)
            return advanced;
        return m_runtime.set_state(state);
    }

    Comet::Result<void, Comet::Error> ParticlePreview::burst() {
        using Result = Comet::Result<void, Comet::Error>;
        if(!m_world)
            return Result::failure({"Select a particle emitter before previewing"});
        auto entity = m_world->find_entity(m_target.entity);
        const auto& settings = entity.get_component<Comet::ParticleEmitterComponent>();
        if(!settings.enabled)
            return Result::failure({"Enable the emitter before previewing a burst"});
        if(!m_runtime.is_active()) {
            if(auto started = m_runtime.start(*m_world, Comet::SceneRuntime::State::Paused);
                !started)
                return started;
            if(settings.burst > 0)
                return Result::success();
        }
        uint32_t count = settings.burst;
        if(count == 0)
            count = std::min(32u, settings.capacity);
        if(!m_world->request_particle_burst(entity, count))
            return Result::failure({"Enable the emitter before previewing a burst"});
        return flush();
    }

    Comet::Result<void, Comet::Error> ParticlePreview::step() {
        if(!m_world)
            return Comet::Result<void, Comet::Error>::failure(
                {"Select a particle emitter before previewing"});
        if(!m_runtime.is_active()) {
            if(auto started = m_runtime.start(*m_world, Comet::SceneRuntime::State::Paused);
                !started)
                return started;
        }
        pause();
        if(auto requested = m_runtime.request_step(); !requested)
            return requested;
        return m_runtime.advance(0);
    }

    Comet::Result<void, Comet::Error> ParticlePreview::advance(double delta_time) {
        return m_runtime.advance(delta_time);
    }

    void ParticlePreview::pause() {
        if(m_runtime.is_active())
            static_cast<void>(m_runtime.set_state(Comet::SceneRuntime::State::Paused));
    }

    void ParticlePreview::stop() {
        static_cast<void>(m_runtime.stop());
    }

    void ParticlePreview::reset(Comet::Scene* scene) {
        if(scene && m_publication && m_target.scene_lifetime == scene->get_lifetime()
            && scene->get_particles() == m_publication)
            scene->set_particles(nullptr);
        stop();
        static_cast<void>(m_runtime.clear_systems());
        m_world.reset();
        m_collision_entities.clear();
        m_publication.reset();
        m_target = {};
    }

    void ParticlePreview::publish_to(Comet::Scene& scene) {
        if(m_target.scene_lifetime != scene.get_lifetime())
            return;
        m_publication = snapshot();
        scene.set_particles(m_publication);
    }

    std::shared_ptr<const Comet::ParticleSnapshot> ParticlePreview::snapshot() const {
        return m_world ? m_world->get_particles() : nullptr;
    }
}
