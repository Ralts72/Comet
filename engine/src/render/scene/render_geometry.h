#pragma once

#include "common/export.h"
#include "common/version.h"
#include "core/geometry.h"
#include "scene/entity_id.h"
#include "render/scene/render_submission.h"
#include <unordered_map>

#include <span>
#include <vector>

namespace Comet {
    struct ResolvedRenderItem;
    struct RenderWorld;

    // 缓存世界界限；逐视图选级仅持有本帧绘制项，不保留组件指针。
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
        // 固定深度程序按源几何界限先粗裁剪；Main 保留实际材质管线的可见性策略。
        const RenderGeometry& prepare_view(const RenderGeometry& world,
            const Math::Mat4& view_projection, bool cull_before_lod = false);
        struct ViewStatistics {
            uint32_t culled_items = 0;
            uint32_t lod_selections = 0;
        };
        [[nodiscard]] ViewStatistics get_view_statistics() const { return m_view_statistics; }
        [[nodiscard]] static float projected_height(
            const BoundingBox& bounds, const Math::Mat4& view_projection);
        [[nodiscard]] static int select_lod(
            const MeshLod& settings, float height, int previous = -1);
        void replace_skinned(
            std::size_t index, const ResolvedRenderItem& item, const BoundingBox& bounds);
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
            bool had_lod = false;
            float bend_height = 0;
        };

        struct LodHistory {
            MeshLod settings;
            int level = -1;
            bool used = false;
        };
        std::unordered_map<EntityId, LodHistory> m_lod_history;
        std::vector<ResolvedRenderItem> m_selected_items;
        std::vector<Item> m_items;
        std::size_t m_lod_count = 0;
        std::vector<BoundingBox> m_skin_bounds;
        std::vector<CachedBounds> m_bounds;
        std::optional<BoundingBox> m_scene_bounds;
        std::optional<BoundingBox> m_cached_scene_bounds;
        VersionStamp<uint64_t> m_scene_version;
        ViewStatistics m_view_statistics;
    };
}
