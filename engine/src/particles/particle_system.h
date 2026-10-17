#pragma once

#include "particles/particle_data.h"
#include "scene/entity_id.h"
#include "scene/entity_uuid.h"
#include "scene/systems/system.h"

#include <map>

namespace Comet {
    class COMET_API ParticleSystem final: public System {
    public:
        struct Statistics {
            uint64_t source_scans = 0;
            uint64_t snapshot_builds = 0;
            uint64_t collision_queries = 0;
        };
        [[nodiscard]] Statistics get_statistics() const { return m_statistics; }
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> fixed_update(Scene&, const Context&) override;
        Result<void, Error> post_update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct Particle {
            Math::Vec3 position;
            Math::Vec3 velocity;
            double age = 0;
        };
        struct Emitter {
            ParticleEmitterComponent settings;
            Math::Mat4 pose{1};
            std::vector<Particle> particles;
            double remainder = 0;
            uint32_t random = 1;
            bool present = false;
            EntityUuid ignored;
            std::vector<GpuParticleSpawn> gpu_spawns;
            uint64_t next_spawn = 0;
            uint32_t gpu_count = 0;
        };
        struct TrailPoint {
            Math::Vec3 position;
            double time;
            bool connected;
        };
        struct Trail {
            TrailComponent settings;
            std::vector<TrailPoint> points;
            bool present = false;
            bool emitting = false;
        };
        Result<void, Error> synchronize(Scene& scene);
        bool configure(Emitter&, const ParticleEmitterComponent&);
        Result<void, Error> emit(
            Emitter& emitter, uint32_t count, double delta = 0, double previous = 0);
        void emit_gpu(Emitter&, uint32_t count, double delta, double previous);
        void trim_gpu(Emitter&, uint32_t count);
        void advance_trails(Scene& scene);
        void publish(Scene& scene);
        static float random_unit(Emitter& emitter);
        Result<void, Error> advance(Particle& particle, const Emitter& emitter, double delta);

        std::map<EntityId, Emitter> m_emitters;
        std::map<EntityId, Trail> m_trails;
        PhysicsAccess* m_physics = nullptr;
        double m_time = 0;
        uint32_t m_count = 0;
        uint32_t m_gpu_count = 0;
        uint64_t m_settings_revision = UINT64_MAX;
        uint64_t m_transform_revision = 0;
        bool m_dirty = false;
        Statistics m_statistics;
    };
}
