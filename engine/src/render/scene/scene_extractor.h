#pragma once

#include "common/export.h"
#include "render/scene/render_scene.h"
#include <entt.hpp>

namespace Comet {
    class Scene;

    class COMET_API SceneExtractor {
    public:
        [[nodiscard]] static RenderScene extract(Scene& scene);
        static void extract(Scene& scene, RenderScene& output);

        SceneExtractor() = default;
        SceneExtractor(const SceneExtractor&) = delete;
        SceneExtractor& operator=(const SceneExtractor&) = delete;

        // 增量更新独占的输出；输出被清空或更换时同时 reset。
        void update(Scene& scene, RenderScene& output);
        void reset() noexcept;

    private:
        static void extract_all(Scene& scene, RenderScene& output, SceneExtractor* slots);
        void update_items(Scene& scene, RenderScene& output);

        uint64_t m_scene_lifetime = 0;
        uint64_t m_revision = 0;
        bool m_matches_storage_order = false;
        std::vector<std::size_t> m_slots_by_entity;
        std::vector<entt::entity> m_entities_by_slot;
    };
}
