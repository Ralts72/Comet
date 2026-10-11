#pragma once

#include "asset/handle.h"
#include "render/lighting.h"
#include "common/export.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"
#include "scene/scene_settings.h"

#include <optional>
#include <memory>
#include <cstddef>
#include <utility>
#include <vector>

namespace Comet {
    struct MaterialOverrides;

    struct COMET_API RenderCamera {
        enum class Projection { Perspective, Orthographic };
        enum class ProjectionIssue {
            InvalidFov,
            InvalidOrthographicHeight,
            InvalidClipPlanes,
            InvalidAspect,
            InvalidView,
        };

        [[nodiscard]] std::optional<ProjectionIssue> projection_issue(float aspect) const;
        [[nodiscard]] std::optional<Math::Mat4> projection_matrix(float aspect) const;

        EntityId entity_id = INVALID_ENTITY_ID;
        bool primary = false;
        Math::Mat4 view_matrix = Math::Mat4(1.0f);
        Projection projection = Projection::Perspective;
        float fov_degrees = 45.0f;
        float orthographic_height = 10.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
    };

    struct RenderItem {
        EntityId entity_id = INVALID_ENTITY_ID;
        uint64_t transform_revision = 0;
        Math::Mat4 model_matrix = Math::Mat4(1.0f);
        AssetHandle mesh_handle = INVALID_ASSET_HANDLE;
        AssetHandle material_handle = INVALID_ASSET_HANDLE;
        std::shared_ptr<const MaterialOverrides> material_overrides;
    };

    // revision 为 0 时逐项核对；局部批次只能接在 base_revision 对应的输出后。
    struct COMET_API RenderItemChanges {
        struct StructuralEdit {
            enum class Type { Append, Remove } type;
            std::size_t slot;
        };

        uint64_t revision = 0;
        uint64_t base_revision = 0;
        bool full_update = true;
        std::vector<std::size_t> slots;
        // 按提取顺序追加或交换末项删除；slots 指向编辑完成后的变化位置。
        std::vector<StructuralEdit> structure;

        void begin_update(bool full);
        [[nodiscard]] bool can_apply(
            uint64_t previous_revision, std::size_t previous_size, std::size_t current_size) const;

        // can_apply 确认批次连续且槽位完整后，共用相同的缓存移动规则。
        template<typename T> void apply_structure(std::vector<T>& items) const {
            for(const auto& edit : structure) {
                if(edit.type == StructuralEdit::Type::Append) {
                    items.emplace_back();
                } else {
                    if(edit.slot + 1 != items.size())
                        items[edit.slot] = std::move(items.back());
                    items.pop_back();
                }
            }
        }
    };

    struct RenderScene {
        uint64_t scene_lifetime = 0;
        std::vector<RenderCamera> cameras;
        std::vector<RenderItem> render_items;
        std::vector<RenderLight> lights;
        SceneEnvironment environment;
        PostProcessSettings post_process;
        RenderItemChanges item_changes;
    };

    struct RenderView {
        enum class CameraSelection { ScenePrimary, Override };

        bool visible = true;
        Math::Vec2u render_size{};
        CameraSelection camera_selection = CameraSelection::ScenePrimary;
        std::optional<RenderCamera> camera_override;
    };
}
