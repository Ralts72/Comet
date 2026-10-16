#pragma once

#include "assets/lighting_bake_input.h"

#include <memory>

namespace Comet {
    class AssetRegistry;
    class Renderer;
    class Scene;
    class ScenePreview;
}

namespace CometEditor {
    class CommandHistory;
    class EditorAssets;

    // Editor 拥有烘焙工作流；渲染只捕获快照，资产事务负责发布 HDR 源。
    class ReflectionBake {
    public:
        struct Input {
            Comet::RenderScene scene;
            Comet::Math::Vec3 position;
            uint32_t resolution;
            float distance;
            std::string fingerprint;
            std::vector<Comet::AssetHandle> dependencies;
        };
        ReflectionBake(
            Comet::Renderer& renderer, EditorAssets& assets, const Comet::AssetRegistry& registry);
        ~ReflectionBake();
        [[nodiscard]] static Comet::Result<Input> describe_input(Comet::Scene& scene,
            Comet::EntityUuid entity, const Comet::AssetDatabase& assets,
            uint64_t shader_fingerprint);
        [[nodiscard]] Comet::Result<void> start(
            Comet::Scene& scene, Comet::EntityUuid entity, const CommandHistory& history);
        [[nodiscard]] Comet::Result<bool> update(Comet::Scene& scene, CommandHistory& history);
        [[nodiscard]] std::string status(Comet::Scene& scene, Comet::EntityUuid entity);
        [[nodiscard]] bool busy() const { return bool(m_capture); }
        void cancel();

    private:
        Comet::Renderer& m_renderer;
        EditorAssets& m_assets;
        const Comet::AssetRegistry& m_registry;
        std::unique_ptr<Comet::ScenePreview> m_capture;
        Comet::EntityUuid m_entity;
        uint64_t m_lifetime = 0;
        uint64_t m_generation = 0;
        std::vector<std::pair<Comet::AssetHandle, uint64_t>> m_versions;
        std::string m_fingerprint;
        LightingBakeInput::StatusCache m_status;
    };
}
