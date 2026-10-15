#include "inspector/particle_preview.h"

#include "particles/particle_system.h"
#include "scene/scene.h"

#include <algorithm>
#include <glm/gtc/quaternion.hpp>

namespace CometEditor {
    ParticlePreview::ParticlePreview() = default;
    ParticlePreview::~ParticlePreview() = default;

    Comet::Result<void, Comet::Error> ParticlePreview::bind(Target target,
        const Comet::ParticleEmitterComponent& settings, const Comet::Math::Mat4& pose) {
        using Result = Comet::Result<void, Comet::Error>;
        if(target.scene_lifetime == 0 || !target.entity)
            return Result::failure({"Particle preview requires a valid scene and entity"});
        if(auto valid = settings.validate(); !valid)
            return Result::failure({valid.error()});
        if(m_target != target) {
            reset();
            m_world = std::make_unique<Comet::Scene>();
            auto entity = m_world->create_entity_with_uuid(target.entity, "Particle Preview");
            entity.add_component<Comet::ParticleEmitterComponent>(settings);
            if(auto added = m_runtime.add_system(std::make_unique<Comet::ParticleSystem>()); !added)
                return added;
            m_target = target;
        }
        auto entity = m_world->find_entity(target.entity);
        const Comet::TransformComponent transform{.translation = Comet::Math::Vec3(pose[3]),
            .rotation = glm::degrees(glm::eulerAngles(glm::quat_cast(Comet::Math::Mat3(pose))))};
        const auto& before = entity.get_component<Comet::TransformComponent>();
        const bool changed = settings != entity.get_component<Comet::ParticleEmitterComponent>()
                             || before.translation != transform.translation
                             || before.rotation != transform.rotation;
        if(!entity.try_set_component(settings) || !entity.try_set_transform(transform))
            return Result::failure({"Invalid particle preview transform"});
        if(changed && m_runtime.is_active())
            return flush();
        return Result::success();
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

    void ParticlePreview::reset() {
        stop();
        static_cast<void>(m_runtime.clear_systems());
        m_world.reset();
        m_target = {};
    }

    std::shared_ptr<const Comet::ParticleSnapshot> ParticlePreview::snapshot() const {
        return m_world ? m_world->get_particles() : nullptr;
    }
}
