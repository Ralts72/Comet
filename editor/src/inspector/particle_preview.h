#pragma once

#include "particles/particle_data.h"
#include "scene/entity_uuid.h"
#include "scene/scene_runtime.h"
#include "physics/physics_service.h"

namespace CometEditor {
    // 独立运行粒子与冻结的碰撞几何；编辑场景只借用不可变的预览结果。
    class ParticlePreview {
    public:
        struct Target {
            uint64_t scene_lifetime = 0;
            Comet::EntityUuid entity;
            bool operator==(const Target&) const = default;
        };

        explicit ParticlePreview(Comet::PhysicsService::MeshSource meshes = {});
        ~ParticlePreview();
        Comet::Result<void, Comet::Error> bind(Target target,
            const Comet::ParticleEmitterComponent& settings, const Comet::Math::Mat4& world_pose,
            Comet::Scene* collision_scene = nullptr, const Comet::TrailComponent* trail = nullptr);
        Comet::Result<void, Comet::Error> play();
        Comet::Result<void, Comet::Error> burst();
        Comet::Result<void, Comet::Error> step();
        Comet::Result<void, Comet::Error> advance(double delta_time);
        void pause();
        void stop();
        void reset(Comet::Scene* scene = nullptr);
        void publish_to(Comet::Scene& scene);

        [[nodiscard]] Target target() const { return m_target; }
        [[nodiscard]] bool playing() const {
            return m_runtime.is_active()
                   && m_runtime.get_state() == Comet::SceneRuntime::State::Running;
        }
        [[nodiscard]] std::shared_ptr<const Comet::ParticleSnapshot> snapshot() const;

    private:
        Comet::Result<void, Comet::Error> flush();
        void synchronize_colliders(Comet::Scene& source);
        Target m_target;
        std::unique_ptr<Comet::Scene> m_world;
        std::unique_ptr<Comet::PhysicsService> m_physics;
        std::vector<Comet::EntityUuid> m_collision_entities;
        std::shared_ptr<const Comet::ParticleSnapshot> m_publication;
        Comet::SceneRuntime m_runtime;
    };
}
