#pragma once

#include "common/export.h"
#include "common/version.h"
#include "core/geometry.h"
#include "scene/entity_id.h"

#include <span>
#include <vector>

namespace Comet {
    struct ResolvedRenderItem;
    struct RenderWorld;

    // 只借用世界数据；未变界限按场景、实体和版本复用，不保留组件指针。
    class COMET_API RenderGeometry {
    public:
        struct Item {
            const ResolvedRenderItem* source;
            const BoundingBox* world_bounds;
        };

        RenderGeometry() = default;
        RenderGeometry(const RenderGeometry&) = delete;
        RenderGeometry& operator=(const RenderGeometry&) = delete;

        void prepare(const RenderWorld& world);
        void clear_frame();
        void reset();
        // 列表和界限只借用到下一次 prepare／clear_frame／reset。
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
        VersionStamp<uint64_t> m_scene_version;
    };
}
