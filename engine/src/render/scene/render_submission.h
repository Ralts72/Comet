#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "render/scene/render_types.h"
#include "render/scene/render_scene.h"
#include "scene/entity_id.h"
#include "render/lighting.h"
#include "scene/scene_settings.h"

#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class Mesh;
    class Material;
    struct MaterialOverrides;
    struct Environment;

    struct MaterialBinding {
        AssetHandle material_handle = INVALID_ASSET_HANDLE;
        std::shared_ptr<const Material> resource;
        std::shared_ptr<const MaterialOverrides> overrides;
        uint64_t asset_revision = 0;
    };

    struct ResolvedRenderItem {
        EntityId entity_id = INVALID_ENTITY_ID;
        uint64_t transform_revision = 0;
        Math::Mat4 model_matrix = Math::Mat4(1.0f);
        AssetHandle mesh_handle = INVALID_ASSET_HANDLE;
        uint64_t mesh_revision = 0;
        std::shared_ptr<Mesh> mesh;
        MaterialBinding material;
    };

    // 与视图选择和输出目标无关的持久世界数据；资源引用由世界和在途 FrameSlot 保活。
    struct RenderWorld {
        uint64_t scene_lifetime = 0;
        uint64_t asset_revision = 0;
        std::vector<RenderCamera> cameras;
        std::vector<ResolvedRenderItem> render_items;
        std::vector<RenderLight> lights;
        SceneEnvironment environment;
        PostProcessSettings post_process;
        uint64_t environment_revision = 0;
        std::shared_ptr<Environment> environment_resource;
        uint64_t scene_revision = 0;
        RenderItemChanges item_changes;
        // 帧边界释放资源后待解析的位置；随世界快照复制，不属于 Scene 变化。
        std::vector<std::size_t> pending_asset_slots;

        // 清空当前世界，保留容器容量供场景切换或延期帧后复用。
        void clear() {
            scene_lifetime = 0;
            asset_revision = 0;
            cameras.clear();
            render_items.clear();
            lights.clear();
            environment = {};
            post_process = {};
            environment_revision = 0;
            environment_resource.reset();
            scene_revision = 0;
            item_changes.revision = 0;
            item_changes.base_revision = 0;
            item_changes.full_update = true;
            item_changes.slots.clear();
            item_changes.structure.clear();
            pending_asset_slots.clear();
        }
    };

    // 只在同步绘制期间借用世界；下一个世界更新前必须完成所有视图的录制。
    struct RenderSubmission {
        const RenderWorld& world;
        std::optional<ViewProjectMatrix> view_project_matrix;
    };
}
