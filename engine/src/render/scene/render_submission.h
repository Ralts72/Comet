#pragma once

#include "asset/content_fwd.h"

#include "asset/handle.h"
#include "core/math_utils.h"
#include "render/scene/render_types.h"
#include "render/scene/render_scene.h"
#include "scene/entity_id.h"
#include "render/lighting.h"
#include "scene/scene_settings.h"

#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Comet {
    struct AnimationPlayback;
    class ShaderProgramArtifact;
    struct MaterialOverrides;
    struct Environment;

    struct MaterialBinding {
        AssetHandle material_handle = INVALID_ASSET_HANDLE;
        std::shared_ptr<const Material> resource;
        std::shared_ptr<const MaterialOverrides> overrides;
        uint64_t asset_revision = 0;
        std::shared_ptr<const ShaderProgramArtifact> program_source;
    };

    struct ResolvedMeshLod {
        MeshLod settings;
        std::array<std::shared_ptr<Mesh>, 2> meshes;
        std::array<uint64_t, 2> revisions{};
    };

    struct ResolvedRenderItem {
        EntityId entity_id = INVALID_ENTITY_ID;
        uint64_t transform_revision = 0;
        std::shared_ptr<const AnimationPlayback> animation;
        Math::Mat4 model_matrix = Math::Mat4(1.0f);
        AssetHandle mesh_handle = INVALID_ASSET_HANDLE;
        uint64_t mesh_revision = 0;
        std::shared_ptr<Mesh> mesh;
        MaterialBinding material;
        std::shared_ptr<const ResolvedMeshLod> lod;
        bool contributes_to_gi = true;
    };

    struct ResolvedReflectionProbe {
        RenderReflectionProbe probe;
        std::shared_ptr<Environment> environment;
        uint64_t revision = 0;
    };

    struct ResolvedIrradianceVolume {
        RenderIrradianceVolume volume;
        std::shared_ptr<Irradiance> resource;
        uint64_t revision = 0;
        uint32_t updated_first = 0;
        uint32_t updated_count = 0;
        uint32_t evaluated_probes = 0;
    };

    struct ResolvedDecal {
        RenderDecal decal;
        std::shared_ptr<Texture> texture;
    };

    // 与视图选择和输出目标无关的持久世界数据；资源引用由世界和在途 FrameSlot 保活。
    struct RenderWorld {
        uint64_t scene_lifetime = 0;
        std::vector<RenderCamera> cameras;
        std::vector<ResolvedRenderItem> render_items;
        std::vector<RenderLight> lights;
        std::vector<ResolvedReflectionProbe> reflection_probes;
        std::vector<ResolvedIrradianceVolume> irradiance_volumes;
        std::vector<ResolvedDecal> decals;
        SceneEnvironment environment;
        PostProcessSettings post_process;
        uint64_t environment_revision = 0;
        std::shared_ptr<Environment> environment_resource;
        RenderItemChanges item_changes;
        std::shared_ptr<const ParticleSnapshot> particles;
        std::vector<std::shared_ptr<Texture>> particle_textures;
        // 随世界快照复制，保证独立消费者的解析批次连续性。
        struct ResolutionCache {
            uint64_t asset_revision = 0;
            uint64_t input_revision = 0;
            std::vector<std::size_t> pending_asset_slots;
            struct MaterialSource {
                std::weak_ptr<const Material> source;
                uint64_t revision;
            };
            // 仅 Main 检查编辑源；GPU 阶段只读取 MaterialBinding 中的独立快照。
            std::unordered_map<AssetHandle, MaterialSource> material_sources;
        } resolution;

        // 清空当前世界，保留容器容量供场景切换或延期帧后复用。
        void clear() {
            scene_lifetime = 0;
            resolution.asset_revision = 0;
            resolution.material_sources.clear();
            cameras.clear();
            render_items.clear();
            lights.clear();
            reflection_probes.clear();
            irradiance_volumes.clear();
            decals.clear();
            particles.reset();
            particle_textures.clear();
            environment = {};
            post_process = {};
            environment_revision = 0;
            environment_resource.reset();
            resolution.input_revision = 0;
            item_changes.revision = 0;
            item_changes.base_revision = 0;
            item_changes.full_update = true;
            item_changes.slots.clear();
            item_changes.structure.clear();
            resolution.pending_asset_slots.clear();
        }
    };

    // 录制期间借用帧拥有的只读世界；保留提交时必须同时保留世界快照。
    struct RenderSubmission {
        const RenderWorld& world;
        std::optional<ViewProjectMatrix> view_project_matrix;
        std::shared_ptr<const RayScene> path_trace;
        PathTraceSettings path_trace_settings;
        std::span<const ResolvedIrradianceVolume> realtime_irradiance;
        IrradianceDebugMode irradiance_debug = IrradianceDebugMode::Off;
    };
}
