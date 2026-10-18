#pragma once

#include "scene/components.h"
#include "scene/systems/system.h"
#include <optional>
#include <unordered_map>
#include <vector>

namespace Comet {
    class MaterialParameterValidator;
}
namespace Comet::Gameplay {
    class COMET_API SplineMotionSystem final: public System {
    public:
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> update(Scene&, const Context&) override;
        Result<void, Error> fixed_update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct State {
            double elapsed = 0;
            EntityUuid path;
            bool active = false, had_controller = false, controller_enabled = false;
        };
        enum class Stage { Start, Fixed, Update };
        Result<void, Error> advance(Scene&, double delta, const InputState* input, Stage);
        void release_camera(Scene&, EntityId, State&);
        std::unordered_map<EntityId, State> m_states;
    };
    class COMET_API WeatherSystem final: public System {
    public:
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        Result<void, Error> apply(Scene&, double time);
        void restore(Scene&) noexcept;
        std::optional<SceneEnvironment> m_environment;
        struct Sun {
            float intensity;
            Math::Vec3 rotation;
        };
        struct Precipitation {
            bool enabled;
            float rate;
            Math::Vec3 wind;
        };
        static void restore_sun(Scene&, EntityId, const Sun&);
        static void restore_precipitation(Scene&, EntityId, const Precipitation&);
        std::unordered_map<EntityId, Sun> m_suns;
        std::unordered_map<EntityId, Precipitation> m_precipitation;
    };
    class COMET_API WaterSurfaceSystem final: public System {
    public:
        explicit WaterSurfaceSystem(const MaterialParameterValidator& materials)
            : m_materials(materials) {}
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> update(Scene&, const Context&) override;

    private:
        Result<void, Error> apply(Scene&, double time);
        std::vector<EntityId> m_active, m_next;
        const MaterialParameterValidator& m_materials;
    };
}
