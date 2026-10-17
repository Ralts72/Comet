#pragma once

#include "asset/handle.h"
#include "asset/data/mesh_lod.h"
#include "render/lighting.h"
#include "common/export.h"
#include "core/math_utils.h"
#include "particles/particle_data.h"
#include "scene/entity_id.h"
#include "scene/scene_settings.h"

#include <algorithm>
#include <optional>
#include <memory>
#include <cstddef>
#include <utility>
#include <vector>

namespace Comet {
    struct AnimationPlayback;
    struct MaterialOverrides;
    struct RayScene;

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
        std::shared_ptr<const AnimationPlayback> animation;
        Math::Mat4 model_matrix = Math::Mat4(1.0f);
        std::optional<MeshLod> lod;
        AssetHandle mesh_handle = INVALID_ASSET_HANDLE;
        AssetHandle material_handle = INVALID_ASSET_HANDLE;
        std::shared_ptr<const MaterialOverrides> material_overrides;
        bool contributes_to_gi = true;
        OutlineSettings outline{.enabled = false};
        Math::Vec4 vertex_bend{0};
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

        // 无连续批次时，仅优化单项增删；调用方仍须核对每个实体和版本。
        template<typename Current, typename Cached>
        static void align_single_edit(
            const std::vector<Current>& items, std::vector<Cached>& cache) {
            const auto previous_size = cache.size();
            if(items.size() != previous_size + 1 && previous_size != items.size() + 1)
                return;
            std::size_t index = 0;
            while(index < std::min(items.size(), previous_size)
                  && items[index].entity_id == cache[index].entity_id)
                ++index;
            if(items.size() > previous_size
                && (index == previous_size
                    || items[index + 1].entity_id == cache[index].entity_id)) {
                cache.emplace_back();
                std::move_backward(cache.begin() + index, cache.end() - 1, cache.end());
                cache[index] = {};
            } else if(previous_size > items.size()
                      && (index == items.size()
                          || items[index].entity_id == cache[index + 1].entity_id)) {
                std::move(cache.begin() + index + 1, cache.end(), cache.begin() + index);
                cache.pop_back();
            }
        }
    };

    struct RenderDecal {
        EntityId entity_id = INVALID_ENTITY_ID;
        AssetHandle texture;
        Math::Mat4 world_to_local{1};
        BoundingBox bounds;
        Math::Vec4 color{1};
        float roughness = .8f;
        float normal_threshold = .2f;
        float edge_fade = .05f;
        uint32_t draw_order = 0;
    };

    struct RenderScene {
        uint64_t scene_lifetime = 0;
        std::vector<RenderCamera> cameras;
        std::vector<RenderItem> render_items;
        std::vector<RenderLight> lights;
        std::vector<RenderReflectionProbe> reflection_probes;
        std::vector<RenderIrradianceVolume> irradiance_volumes;
        std::vector<RenderDecal> decals;
        SceneEnvironment environment;
        PostProcessSettings post_process;
        RenderItemChanges item_changes;
        std::shared_ptr<const ParticleSnapshot> particles;
        std::vector<ParticleWorldCollider> particle_colliders;
        std::vector<ParticleMeshCollider> particle_meshes;
    };

    struct COMET_API PathTraceSettings {
        uint32_t samples = 256;
        uint32_t bounces = 4;
        uint32_t seed = 0;
        uint64_t reset_generation = 0;
        uint32_t max_dimension = 512;
        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] Math::Vec2u image_size(Math::Vec2u output) const;
        bool operator==(const PathTraceSettings&) const = default;
    };

    enum class IrradianceDebugMode : uint32_t { Off, Status, Lighting };

    struct RenderView {
        enum class CameraSelection { ScenePrimary, Override };

        bool visible = true;
        Math::Vec2u render_size{};
        CameraSelection camera_selection = CameraSelection::ScenePrimary;
        std::optional<RenderCamera> camera_override;
        bool path_trace_enabled = false;
        std::shared_ptr<const RayScene> path_trace;
        PathTraceSettings path_trace_settings;
        std::shared_ptr<const RayScene> particle_collisions;
        IrradianceDebugMode irradiance_debug = IrradianceDebugMode::Off;
    };
}
