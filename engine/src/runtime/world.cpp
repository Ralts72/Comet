#include "runtime/world.h"
#include "audio/audio_service.h"
#include "scene/scene.h"
#include "render/material/material_programs.h"
#include "asset/data/navigation_data.h"
#include "gameplay/root_motion.h"
#include "gameplay/navigation_system.h"
#include "gameplay/character_animation.h"
#include "gameplay/foot_placement.h"
#include "gameplay/pose_constraints.h"
#include "gameplay/wind_sway.h"
#include "gameplay/world_effects.h"
#include "animation/animation_system.h"
#include "particles/particle_system.h"
#include "render/resource/mesh.h"
#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "scene/systems/audio_system.h"
#include "scene/systems/camera_controller.h"
#include "scene/systems/physics_system.h"
#include "scene/systems/script_system.h"

namespace Comet {
    World::World(AssetRegistry& assets, MaterialProgramValidator& materials)
        : m_assets(assets), m_materials(materials) {}
    World::~World() {
        shutdown();
    }
    void World::shutdown() {
        if(auto stopped = m_runtime.stop(); !stopped)
            LOG_FATAL("Cannot shut down World during System execution");
        if(auto cleared = m_runtime.clear_systems(); !cleared)
            LOG_FATAL("Cannot release executing World systems");
        if(auto detached = m_runtime.set_services({}); !detached)
            LOG_FATAL("Cannot detach World services");
        m_audio.reset();
        m_physics.reset();
    }
    std::unique_ptr<Scene> World::replace_scene(std::unique_ptr<Scene> scene) noexcept {
        if(auto stopped = m_runtime.stop(); !stopped)
            LOG_FATAL("Cannot replace Scene during System execution");
        m_scene.swap(scene);
        return scene;
    }
    Result<void, Error> World::apply_audio_settings(AudioSettings settings) {
        if(auto valid = settings.validate(); !valid)
            return Result<void, Error>::failure({valid.error()});
        if(m_audio)
            if(auto applied = m_audio->apply_settings(settings); !applied)
                return applied;
        m_audio_settings = settings;
        return Result<void, Error>::success();
    }
    Result<void, Error> World::install_default_systems(PhysicsService::MeshSource meshes) {
        if(!m_audio) {
            m_audio = std::make_unique<AudioService>(m_assets);
            if(auto applied = m_audio->apply_settings(m_audio_settings); !applied)
                return applied;
        }
        if(!m_physics)
            m_physics = std::make_unique<PhysicsService>(std::move(meshes));
        const auto add_system = [&](std::unique_ptr<System> system,
                                    System::Order order = System::Order::Gameplay) {
            return m_runtime.add_system(std::move(system), order);
        };
        auto navigation = std::make_unique<Gameplay::NavigationSystem>(
            [this](Math::Vec3 minimum, Math::Vec3 maximum) {
                return m_physics->static_triangles(minimum, maximum);
            },
            [this](AssetHandle handle) { return m_assets.resolve<NavigationData>(handle); });
        auto* navigation_service = &navigation->service();
        if(auto added = add_system(std::move(navigation), System::Order::Input); !added)
            return added;
        if(auto configured = m_runtime.set_services({.audio = m_audio.get(),
               .physics = m_physics.get(),
               .navigation = navigation_service});
            !configured)
            return configured;
        if(auto added = add_system(std::make_unique<Gameplay::SplineMotionSystem>()); !added)
            return added;
        if(auto added =
                add_system(std::make_unique<CameraControllerSystem>(), System::Order::Camera);
            !added)
            return added;
        if(auto added = add_system(std::make_unique<ScriptSystem>(m_assets, &m_materials)); !added)
            return added;
        if(auto added = add_system(
               std::make_unique<Gameplay::CharacterAnimationSystem>(), System::Order::Gameplay);
            !added)
            return added;
        if(auto added = add_system(std::make_unique<AnimationSystem>(
                                       [this](AssetHandle handle) {
                                           auto mesh = m_assets.resolve<Mesh>(handle);
                                           return mesh ? mesh->animation_data() : nullptr;
                                       },
                                       [this](AssetHandle handle) {
                                           return m_assets.resolve<AnimationController>(handle);
                                       },
                                       [this](AssetHandle handle) {
                                           return m_assets.resolve<AnimationLibrary>(handle);
                                       },
                                       Gameplay::apply_root_motion),
               System::Order::Animation);
            !added)
            return added;
        if(auto added =
                add_system(std::make_unique<PhysicsSystem>(*m_physics), System::Order::Physics);
            !added)
            return added;
        if(auto added = add_system(
               std::make_unique<Gameplay::FootPlacementSystem>(), System::Order::Constraints);
            !added)
            return added;
        if(auto added = add_system(
               std::make_unique<Gameplay::PoseConstraintSystem>(), System::Order::Constraints);
            !added)
            return added;
        if(auto added =
                add_system(std::make_unique<Gameplay::WeatherSystem>(), System::Order::Environment);
            !added)
            return added;
        if(auto added = add_system(std::make_unique<Gameplay::WaterSurfaceSystem>(m_materials),
               System::Order::Environment);
            !added)
            return added;
        if(auto added = add_system(
               std::make_unique<Gameplay::WindSwaySystem>(), System::Order::Environment);
            !added)
            return added;
        if(auto added = add_system(std::make_unique<ParticleSystem>(), System::Order::Particles);
            !added)
            return added;
        return add_system(std::make_unique<AudioSystem>(*m_audio), System::Order::Audio);
    }

}
