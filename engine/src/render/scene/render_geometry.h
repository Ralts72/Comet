#pragma once

#include "common/export.h"
#include "core/geometry.h"
#include "render/scene/render_submission.h"

#include <span>

namespace Comet {
    // 只借用本帧提交；未变界限按场景、实体和版本复用，不保留组件指针。
    class COMET_API RenderGeometry {
    public:
        struct Item {
            const ResolvedRenderItem* source;
            std::optional<BoundingBox> world_bounds;
        };

        void prepare(const RenderSubmission& submission);
        void clear();
        [[nodiscard]] std::span<const Item> get_items() const { return m_items; }
        [[nodiscard]] const std::optional<BoundingBox>& get_scene_bounds() const {
            return m_scene_bounds;
        }

    private:
        struct CachedBounds {
            EntityId entity_id = INVALID_ENTITY_ID;
            uint64_t transform_revision = 0;
            uint64_t mesh_revision = 0;
            std::optional<BoundingBox> bounds;
        };

        std::vector<Item> m_items;
        std::vector<CachedBounds> m_bounds;
        std::optional<BoundingBox> m_scene_bounds;
        std::optional<BoundingBox> m_cached_scene_bounds;
        uint64_t m_scene_lifetime = 0;
        uint64_t m_revision = 0;
    };
}
