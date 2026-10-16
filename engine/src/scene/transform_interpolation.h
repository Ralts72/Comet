#pragma once

#include "scene/components.h"
#include <entt.hpp>
#include <unordered_map>
#include <vector>

namespace Comet {
    class Scene;
    class SceneRuntime;
    class Entity;
    class SceneExtractor;

    // Scene 的临时呈现数据；固定步记录本地姿态，渲染提取读取独立世界变换。
    class TransformInterpolation final {
        friend class Scene;
        friend class SceneRuntime;
        friend class Entity;
        friend class SceneExtractor;

        struct Presented {
            WorldTransformComponent world;
            uint64_t source_revision = 0;
            bool active = false;
        };

        void begin_fixed(Scene& scene);
        void end_fixed(Scene& scene);
        void record(entt::entity handle, const TransformComponent& previous);
        void discontinuity(Scene& scene, entt::entity handle);
        void forget(entt::entity handle);
        void set_alpha(Scene& scene, float alpha);
        void reset(Scene& scene);
        void invalidate(Scene& scene);
        void mark_subtree(Scene& scene, entt::entity handle);
        void update_presented(Scene& scene, entt::entity handle);
        const WorldTransformComponent& world(Scene& scene, entt::entity handle);
        bool active() const noexcept { return m_alpha != 1 && !m_presented.empty(); }

        bool m_fixed = false;
        float m_alpha = 1;
        std::unordered_map<entt::entity, TransformComponent> m_previous;
        std::unordered_map<entt::entity, Presented> m_presented;
        entt::sparse_set m_discontinuities;
        std::vector<entt::entity> m_work;
    };
}
