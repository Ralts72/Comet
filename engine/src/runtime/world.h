#pragma once

#include "audio/audio_settings.h"
#include "physics/physics_service.h"
#include "scene/scene_runtime.h"

namespace Comet {
    class AssetRegistry;
    class AudioService;
    class MaterialProgramValidator;

    class COMET_API World final {
    public:
        World(AssetRegistry& assets, MaterialProgramValidator& materials);
        ~World();
        World(const World&) = delete;
        World& operator=(const World&) = delete;
        [[nodiscard]] Scene* scene() { return m_scene.get(); }
        [[nodiscard]] const Scene* scene() const { return m_scene.get(); }
        [[nodiscard]] SceneRuntime& runtime() { return m_runtime; }
        [[nodiscard]] const SceneRuntime& runtime() const { return m_runtime; }
        [[nodiscard]] std::unique_ptr<Scene> replace_scene(std::unique_ptr<Scene> scene) noexcept;
        [[nodiscard]] Result<void, Error> install_default_systems(
            PhysicsService::MeshSource meshes);
        [[nodiscard]] Result<void, Error> apply_audio_settings(AudioSettings settings);
        [[nodiscard]] const AudioSettings& audio_settings() const { return m_audio_settings; }
        void shutdown();

    private:
        AssetRegistry& m_assets;
        MaterialProgramValidator& m_materials;
        std::unique_ptr<Scene> m_scene;
        std::unique_ptr<AudioService> m_audio;
        std::unique_ptr<PhysicsService> m_physics;
        AudioSettings m_audio_settings;
        SceneRuntime m_runtime;
    };
}
