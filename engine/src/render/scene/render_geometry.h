#pragma once

#include "common/export.h"
#include "core/geometry.h"
#include "render/scene/render_submission.h"

#include <span>

namespace Comet {
    // 只借用本帧提交；主材质与阴影共用一次世界界限计算。
    class COMET_API RenderGeometry {
    public:
        struct Item {
            const ResolvedRenderItem* source;
            std::optional<BoundingBox> world_bounds;
        };

        void prepare(std::span<const ResolvedRenderItem> items);
        void clear();
        [[nodiscard]] std::span<const Item> get_items() const { return m_items; }
        [[nodiscard]] const std::optional<BoundingBox>& get_scene_bounds() const {
            return m_scene_bounds;
        }

    private:
        std::vector<Item> m_items;
        std::optional<BoundingBox> m_scene_bounds;
    };
}
