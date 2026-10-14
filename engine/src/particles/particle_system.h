#pragma once

#include "particles/particle_data.h"
#include "scene/entity_id.h"
#include "scene/systems/system.h"

#include <map>

namespace Comet {
    class COMET_API ParticleSystem final: public System {
    public:
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
        };
        Result<void, Error> synchronize(Scene& scene);
        void emit(Emitter& emitter, uint32_t count, double delta = 0, double previous = 0);
        void publish(Scene& scene);
        static float random_unit(Emitter& emitter);
        static void advance(Particle& particle, const Emitter& emitter, double delta);

        std::map<EntityId, Emitter> m_emitters;
        uint32_t m_count = 0;
    };
}
