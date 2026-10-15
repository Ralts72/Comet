#pragma once

#include "common/export.h"
#include "asset/registry.h"
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
            // 候选只用于一次帧边界提交；跨帧撤销保存 CPU 材质并重新准备。
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
            uint64_t drawn_triangles = 0;
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

        class COMET_API ShaderUpdate {
        public:
            ShaderUpdate(ShaderUpdate&&) noexcept;
            ShaderUpdate& operator=(ShaderUpdate&&) noexcept;
            ~ShaderUpdate();
            [[nodiscard]] const ReloadReport& report() const;
            void publish() &&;

        private:
            friend class MaterialRenderer;
            struct State;
            explicit ShaderUpdate(std::unique_ptr<State> state);
            std::unique_ptr<State> m_state;
        };

        static Result<std::unique_ptr<MaterialRenderer>, GraphicsError> create(Device& device,
            PipelineManager& pipelines, RenderResources& resources, uint32_t frame_slot_count,
            SampleCount samples, const MaterialShaders* shaders = nullptr,
            MaterialPrograms* programs = nullptr, std::optional<float> max_anisotropy = {});
        [[nodiscard]] Result<ShaderUpdate, GraphicsError> prepare_shaders(
            PipelineManager& pipelines, const MaterialShaders& shaders, SampleCount samples);
        // 帧边界提交任意完整顶点/片元程序对；所有候选成功后才替换。
        Result<ReloadReport, GraphicsError> reload_shaders(
            PipelineManager& pipelines, const MaterialShaders& shaders, SampleCount samples);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>> get_material_layouts()
            const;
        // 调用方先固定材质快照和 Shader 来源；GPU 准备不再复制编辑源或查询注册表。
        [[nodiscard]] Result<MaterialUpdate, GraphicsError> prepare_material_update(
            AssetHandle handle, const std::shared_ptr<const Material>& snapshot,
            const std::shared_ptr<const ShaderProgramArtifact>& program_source);
        [[nodiscard]] static Result<std::vector<MaterialUpdate>, GraphicsError>
        prepare_material_updates(std::span<MaterialRenderer* const> renderers, AssetHandle handle,
            const std::shared_ptr<const Material>& snapshot,
            const std::shared_ptr<const ShaderProgramArtifact>& program_source);
        [[nodiscard]] Result<void, GraphicsError> prepare_programs(const RenderWorld& world);
        // 所有目标的候选准备完成后，才发布共享布局和各自的 Pipeline。
        [[nodiscard]] static Result<void, GraphicsError> prepare_programs(
            std::span<MaterialRenderer* const> renderers, const RenderWorld& world);
        // 调用方须先等待槽位、开启场景通道并设置视口与裁剪区域。
        // shadow_map 必须已处于片元 SampledRead；即使关闭阴影也需有效采样绑定。
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> prepare(
            FrameScheduler& frames, const RenderSubmission& submission,
            const RenderGeometry& geometry, const LightingData& lighting,
            const std::shared_ptr<ImageView>& shadow_map,
            const std::shared_ptr<ImageView>& ao_depth = {});
        // 准备一次、按透明深度分段录制；本帧结束后释放借用的世界数据。
        [[nodiscard]] std::span<const float> transparent_depths() const {
            return m_transparent_depths;
        }
        void record_opaque(FrameScheduler& frames, std::vector<QueueSemaphoreSubmit>& waits);
        void record_transparent(FrameScheduler& frames, size_t first, size_t count,
            std::vector<QueueSemaphoreSubmit>& waits);
        void clear_draws() {
            m_draw_queue.clear();
            m_view_geometry.clear_frame();
            m_transparent_depths.clear();
        }
        [[nodiscard]] Statistics get_statistics() const;
        void reset_statistics() { m_statistics = {}; }
        void remove_assets(const AssetRegistry::Removals& removals);

    private:
        MaterialRenderer(Device& device, PipelineManager& pipelines, SampleCount samples,
            MaterialPrograms* programs);
        Result<void, GraphicsError> initialize(PipelineManager& pipelines,
            RenderResources& resources, uint32_t frame_slot_count, SampleCount samples,
            const MaterialShaders* shaders, std::optional<float> max_anisotropy);

        struct SurfacePipelines {
            std::shared_ptr<Pipeline> pipeline;
            std::shared_ptr<Pipeline> instanced_pipeline;
        };
        struct PipelineState {
            AssetHandle shader_program;
            std::shared_ptr<const MaterialLayout> layout;
            std::shared_ptr<DescriptorSetLayout> material_layout;
            std::shared_ptr<Pipeline> pipeline;
            std::shared_ptr<Pipeline> instanced_pipeline;
            bool static_mesh_bounds = false;
            SampleCount samples = SampleCount::Count1;
            std::shared_ptr<Shader> vertex, fragment, instanced_vertex;
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
            std::shared_ptr<const ShaderProgramArtifact> requested_source;
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
            std::shared_ptr<ImageView> ao_depth;
            std::shared_ptr<Sampler> environment_sampler;
            std::shared_ptr<Environment> environment;
            std::optional<DescriptorSet> descriptor;
            InstanceBuffer instances;
        };
        struct MaterialResources {
            std::shared_ptr<const PipelineState> pipeline;
            std::shared_ptr<const PreparedMaterial> prepared;
            SurfacePipelines draws;
            std::optional<SurfacePipelines> mirrored_draws;
            std::vector<std::shared_ptr<Texture>> textures;
            std::shared_ptr<Sampler> sampler;
            std::shared_ptr<CPUBuffer> parameters;
            std::shared_ptr<DescriptorPool> pool;
            std::optional<DescriptorSet> descriptor;
        };
        struct PreparedInputKey {
            // 非拥有指针仅在同一 serial 内比较。
            uint64_t serial = 0;
            uint64_t revision = 0;
            const Material* source = nullptr;
            const MaterialOverrides* overrides = nullptr;
            const PipelineState* pipeline = nullptr;

            bool operator==(const PreparedInputKey&) const = default;
        };
        struct CachedMaterial {
            std::shared_ptr<MaterialResources> resources;
            std::shared_ptr<const MaterialOverrides> overrides;
            std::shared_ptr<const PreparedMaterial> failed_candidate;
            std::weak_ptr<const PipelineState> failed_pipeline;
            uint64_t retry_after_serial = 0;
            std::string preparation_error;
            PreparedInputKey prepared_input;
            const ResolvedRenderItem* requested_input = nullptr; // 程序准备借用，同步时清空。
            bool used = false;
        };
        struct ProjectUpdate {
            std::shared_ptr<const PipelineState> pipeline;
            MaterialRuntimeCache prepared;
            std::map<MaterialInstanceKey, CachedMaterial> materials;
        };
        struct DrawCandidate {
            const ResolvedRenderItem* item;
            bool mirrored;
        };
        struct DrawItem {
            const ResolvedRenderItem* item;
            std::shared_ptr<MaterialResources> material;
            uint32_t instance_count = 1;
            uint32_t first_instance = 0;
            bool mirrored = false;
            float camera_depth = 0;
        };

        void sync_world_inputs(const RenderWorld& world);
        void sync_runtime_instances();
        void sync_program_inputs();
        void update_frame_resources(FrameScheduler& frames, const RenderSubmission& submission,
            const LightingData& lighting, const std::shared_ptr<ImageView>& shadow_map,
            const std::shared_ptr<ImageView>& ao_depth, std::vector<QueueSemaphoreSubmit>& waits);
        Result<void, GraphicsError> prepare_draw_queue(const RenderGeometry& geometry,
            uint64_t frame_serial, const Frustum* frustum, const Math::Mat4& view);
        void collect_visible_draws(const RenderGeometry& geometry, const Frustum* frustum);
        Result<void, GraphicsError> prepare_draw_materials(
            uint64_t frame_serial, const Math::Mat4& view);
        [[nodiscard]] const std::shared_ptr<const PipelineState>& find_pipeline(
            const Material& material) const;
        [[nodiscard]] bool can_cull(const MaterialBinding& material) const;
        void record_draws(FrameScheduler& frames, std::span<const DrawItem> queue,
            std::vector<QueueSemaphoreSubmit>& waits);
        void append_material_draws(
            std::span<DrawCandidate> items, const std::shared_ptr<MaterialResources>& material);
        Result<void, GraphicsError> upload_instances(FrameScheduler& frames);
        void collect_unused_materials(uint64_t frame_serial);

        Result<std::shared_ptr<const PipelineState>, GraphicsError> create_pipeline(
            PipelineManager& pipelines, const std::shared_ptr<Shader>& vertex,
            const std::shared_ptr<Shader>& fragment, std::shared_ptr<const MaterialLayout> layout,
            SampleCount samples, std::shared_ptr<DescriptorSetLayout> material_layout,
            AssetHandle shader_program = INVALID_ASSET_HANDLE,
            const std::shared_ptr<Shader>& instanced_vertex = nullptr,
            bool static_mesh_bounds = false);
        Result<SurfacePipelines, GraphicsError> create_surface_pipelines(PipelineManager& pipelines,
            const PipelineState& program, MaterialSurface surface, bool mirrored = false);
        Result<void, GraphicsError> prepare_builtin_pipeline(PipelineManager& pipelines,
            const MaterialShaderDefinition& definition, const MaterialShaderProgram& code,
            SampleCount samples,
            std::unordered_map<std::string, std::shared_ptr<const PipelineState>>& candidates,
            ReloadReport& report);
        static Result<void, GraphicsError> refresh_program(
            std::span<MaterialRenderer* const> renderers, AssetHandle handle,
            const std::string& template_name,
            const std::shared_ptr<const ShaderProgramArtifact>& source);
        Result<MaterialUpdate, GraphicsError> prepare_material_bindings(
            AssetHandle handle, const std::shared_ptr<const Material>& material);
        Result<ProjectUpdate, GraphicsError> prepare_project_pipeline(AssetHandle handle,
            const std::string& template_name, ProjectPipeline& active,
            const std::shared_ptr<const ShaderProgramArtifact>& version);
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
        std::shared_ptr<Texture> m_normal_texture;
        std::shared_ptr<Environment> m_empty_environment;
        std::shared_ptr<DescriptorSetLayout> m_frame_layout;
        std::vector<std::shared_ptr<FrameResources>> m_frames;
        std::unordered_map<std::string, std::shared_ptr<const PipelineState>> m_pipelines;
        std::shared_ptr<MaterialResources> m_error_material;
        std::map<std::pair<AssetHandle, std::string>, ProjectPipeline> m_project_pipelines;
        MaterialRuntimeCache m_prepared;
        std::map<MaterialInstanceKey, CachedMaterial> m_materials;
        std::unordered_map<AssetHandle, uint64_t> m_unsupported;
        std::vector<size_t> m_material_order;
        std::vector<DrawCandidate> m_draw_candidates;
        std::vector<DrawItem> m_draw_queue;
        std::vector<float> m_transparent_depths;
        std::vector<Math::Mat4> m_instance_transforms;
        RenderGeometry m_view_geometry;
        Statistics m_statistics;
        bool m_has_runtime_instances = false;
    };
}
