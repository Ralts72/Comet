#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/queue.h"
#include "render/material/material_runtime.h"
#include "render/material/material_shader.h"
#include "render/scene/render_submission.h"
#include "render/scene/render_geometry.h"
#include "render/resource/instance_buffer.h"

#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Comet {
    class AssetRegistry;
    class CPUBuffer;
    class Device;
    class MaterialLayout;
    class FrameScheduler;
    class Frustum;
    class Pipeline;
    class PipelineManager;
    class RenderResources;
    class Sampler;
    class Shader;
    class ImageView;
    class ShaderProgramArtifact;
    class MaterialPrograms;

    class COMET_API MaterialRenderer {
        struct MaterialResources;

    public:
        class COMET_API MaterialUpdate {
        public:
            MaterialUpdate(MaterialUpdate&&) = default;
            MaterialUpdate& operator=(MaterialUpdate&&) = default;
            // 帧边界发布；编辑器可保留回退候选，期间不可重建 renderer 或发布 Shader。
            void publish() &&;

        private:
            friend class MaterialRenderer;
            MaterialUpdate() = default;
            MaterialRenderer* m_owner = nullptr;
            AssetHandle m_handle;
            std::shared_ptr<const Material> m_source;
            MaterialRuntimeCache m_prepared;
            std::shared_ptr<MaterialResources> m_resources;
        };

        struct Statistics {
            uint32_t render_items = 0;
            uint32_t culled_items = 0;
            uint32_t draw_calls = 0;
            uint32_t drawn_instances = 0;
            uint32_t instanced_draw_calls = 0;
            uint64_t instance_upload_bytes = 0;
            uint32_t pipeline_binds = 0;
            uint32_t material_binds = 0;
            uint32_t mesh_binds = 0;
            uint32_t material_preparations = 0;
            uint32_t material_versions_created = 0;
            uint32_t material_bindings_created = 0;
            uint32_t cached_material_versions = 0;
            uint32_t frame_set_count = 0;
            uint32_t light_count = 0;
            uint32_t excess_lights = 0;
            uint32_t invalid_lights = 0;
        };
        struct ReloadReport {
            uint32_t pipelines = 0;
            uint32_t material_versions = 0;
            uint32_t material_bindings = 0;
            double pipeline_preparation_ms = 0;
            double candidate_copy_ms = 0;
            double material_cpu_ms = 0;
            double material_gpu_ms = 0;
        };

        static Result<std::unique_ptr<MaterialRenderer>, GraphicsError> create(Device& device,
            PipelineManager& pipelines, RenderResources& resources, uint32_t frame_slot_count,
            SampleCount samples, const MaterialShaders* shaders = nullptr,
            MaterialPrograms* programs = nullptr);
        // 帧边界提交任意完整顶点/片元程序对；所有候选成功后才替换。
        Result<ReloadReport, GraphicsError> reload_shaders(
            PipelineManager& pipelines, const MaterialShaders& shaders, SampleCount samples);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>> get_material_layouts()
            const;
        [[nodiscard]] Result<MaterialUpdate, GraphicsError> prepare_material_update(
            AssetHandle handle, const std::shared_ptr<const Material>& material);
        [[nodiscard]] Result<void, GraphicsError> prepare_programs(
            const RenderSubmission& submission);
        // 调用方须先等待槽位、开启场景通道并设置视口与裁剪区域。
        // shadow_map 必须已处于片元 SampledRead；即使关闭阴影也需有效采样绑定。
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, const RenderSubmission& submission,
            const RenderGeometry& geometry, const LightingData& lighting,
            const std::shared_ptr<ImageView>& shadow_map);
        [[nodiscard]] Statistics get_statistics() const;
        void reset_statistics() { m_statistics = {}; }
        void collect_removed_assets(const AssetRegistry& assets);

    private:
        MaterialRenderer(Device& device, PipelineManager& pipelines, SampleCount samples,
            MaterialPrograms* programs);
        Result<void, GraphicsError> initialize(PipelineManager& pipelines,
            RenderResources& resources, uint32_t frame_slot_count, SampleCount samples,
            const MaterialShaders* shaders);

        struct PipelineState {
            AssetHandle shader_program;
            std::shared_ptr<const MaterialLayout> layout;
            std::shared_ptr<DescriptorSetLayout> material_layout;
            std::shared_ptr<Pipeline> pipeline;
            std::shared_ptr<Pipeline> instanced_pipeline;
            bool static_mesh_bounds = false;
        };
        struct MaterialInput {
            std::shared_ptr<const Material> source;
            uint64_t revision = 0;
            std::shared_ptr<const MaterialOverrides> overrides;
            const MaterialBinding* requested_input = nullptr;
        };
        using MaterialInputs = std::map<MaterialInstanceKey, MaterialInput>;
        struct ProjectPipeline {
            enum class FailureCause { Shader, Overrides, Materials };

            std::shared_ptr<const ShaderProgramArtifact> source;
            std::shared_ptr<const ShaderProgramArtifact> failed_source;
            std::shared_ptr<const PipelineState> pipeline;
            MaterialInputs materials;
            FailureCause failure_cause = FailureCause::Shader;
        };
        struct FrameResources {
            std::shared_ptr<DescriptorSetLayout> layout;
            std::shared_ptr<DescriptorPool> pool;
            std::shared_ptr<CPUBuffer> buffer;
            std::shared_ptr<CPUBuffer> lighting;
            std::shared_ptr<Sampler> shadow_sampler;
            std::shared_ptr<ImageView> shadow_map;
            std::shared_ptr<Sampler> environment_sampler;
            std::shared_ptr<Environment> environment;
            std::optional<DescriptorSet> descriptor;
            InstanceBuffer instances;
        };
        struct MaterialResources {
            std::shared_ptr<const PipelineState> pipeline;
            std::shared_ptr<const PreparedMaterial> prepared;
            std::vector<std::shared_ptr<Texture>> textures;
            std::shared_ptr<Sampler> sampler;
            std::shared_ptr<CPUBuffer> parameters;
            std::shared_ptr<DescriptorPool> pool;
            std::optional<DescriptorSet> descriptor;
        };
        struct CachedMaterial {
            std::shared_ptr<MaterialResources> resources;
            std::shared_ptr<const MaterialOverrides> overrides;
            std::shared_ptr<const PreparedMaterial> failed_candidate;
            std::weak_ptr<const PipelineState> failed_pipeline;
            uint64_t retry_after_serial = 0;
            std::string preparation_error;
            // 帧内输入身份；非拥有指针仅在同一 serial 内比较。
            uint64_t prepared_serial = 0;
            uint64_t prepared_revision = 0;
            const Material* prepared_source = nullptr;
            const MaterialOverrides* prepared_overrides = nullptr;
            const PipelineState* prepared_pipeline = nullptr;
            const ResolvedRenderItem* requested_input = nullptr; // 程序准备借用，同步时清空。
            bool used = false;
        };
        struct DrawItem {
            const ResolvedRenderItem* item;
            std::shared_ptr<MaterialResources> material;
            uint32_t instance_count = 1;
            uint32_t first_instance = 0;
        };

        void sync_runtime_instances();
        void sync_program_inputs();
        void update_frame_resources(FrameScheduler& frames, const RenderSubmission& submission,
            const LightingData& lighting, const std::shared_ptr<ImageView>& shadow_map,
            std::vector<QueueSemaphoreSubmit>& waits);
        Result<void, GraphicsError> prepare_draw_queue(
            const RenderGeometry& geometry, uint64_t frame_serial, const Frustum* frustum);
        void collect_visible_draws(const RenderGeometry& geometry, const Frustum* frustum);
        Result<void, GraphicsError> prepare_draw_materials(uint64_t frame_serial);
        [[nodiscard]] const std::shared_ptr<const PipelineState>& find_pipeline(
            const Material& material) const;
        [[nodiscard]] bool can_cull(const MaterialBinding& material) const;
        void record_draws(FrameScheduler& frames, std::span<const DrawItem> queue,
            std::vector<QueueSemaphoreSubmit>& waits);
        void append_material_draws(std::span<const ResolvedRenderItem*> items,
            const std::shared_ptr<MaterialResources>& material);
        Result<void, GraphicsError> upload_instances(FrameScheduler& frames);
        void collect_unused_materials(uint64_t frame_serial);

        Result<std::shared_ptr<const PipelineState>, GraphicsError> create_pipeline(
            PipelineManager& pipelines, const std::shared_ptr<Shader>& vertex,
            const std::shared_ptr<Shader>& fragment, std::shared_ptr<const MaterialLayout> layout,
            SampleCount samples, std::shared_ptr<DescriptorSetLayout> material_layout,
            AssetHandle shader_program = INVALID_ASSET_HANDLE,
            const std::shared_ptr<Shader>& instanced_vertex = nullptr,
            bool static_mesh_bounds = false);
        Result<void, GraphicsError> prepare_builtin_pipeline(PipelineManager& pipelines,
            const MaterialShaderDefinition& definition, const MaterialShaderProgram& code,
            SampleCount samples,
            std::unordered_map<std::string, std::shared_ptr<const PipelineState>>& candidates,
            ReloadReport& report);
        Result<void, GraphicsError> install_project_pipeline(AssetHandle handle,
            const std::string& template_name, const std::shared_ptr<const PipelineState>& builtin,
            ProjectPipeline& active, const std::shared_ptr<const ShaderProgramArtifact>& version);
        Result<std::shared_ptr<const PipelineState>, GraphicsError> project_pipeline(
            AssetHandle handle, const std::string& template_name);
        // 成功空值表示本次无可绘制版本；失败表示不能继续当前帧。
        [[nodiscard]] Result<std::shared_ptr<MaterialResources>, GraphicsError> prepare_material(
            const MaterialBinding& material, uint64_t frame_serial);
        Result<std::shared_ptr<MaterialResources>, GraphicsError> create_material(
            const std::shared_ptr<const PreparedMaterial>& prepared,
            const std::shared_ptr<const PipelineState>& pipeline,
            const std::shared_ptr<MaterialResources>& previous);

        Device& m_device;
        PipelineManager& m_pipeline_manager;
        SampleCount m_samples;
        MaterialPrograms* m_programs;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Texture> m_white_texture;
        std::shared_ptr<Environment> m_empty_environment;
        std::shared_ptr<DescriptorSetLayout> m_frame_layout;
        std::vector<std::shared_ptr<FrameResources>> m_frames;
        std::unordered_map<std::string, std::shared_ptr<const PipelineState>> m_pipelines;
        std::map<std::pair<AssetHandle, std::string>, ProjectPipeline> m_project_pipelines;
        MaterialRuntimeCache m_prepared;
        std::map<MaterialInstanceKey, CachedMaterial> m_materials;
        std::unordered_map<AssetHandle, uint64_t> m_unsupported;
        std::vector<const ResolvedRenderItem*> m_draw_candidates;
        std::vector<DrawItem> m_draw_queue;
        std::vector<Math::Mat4> m_instance_transforms;
        Statistics m_statistics;
        bool m_has_runtime_instances = false;
    };
}
