#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/queue.h"
#include "render/material/material_runtime.h"
#include "render/material/material_shader.h"
#include "render/scene/render_submission.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Comet {
    class CPUBuffer;
    class Device;
    class MaterialLayout;
    class FrameScheduler;
    class Pipeline;
    class PipelineManager;
    class RenderResources;
    class Sampler;
    class Shader;
    class ImageView;

    class COMET_API MaterialRenderer {
        struct MaterialResources;

    public:
        class COMET_API MaterialUpdate {
        public:
            MaterialUpdate(MaterialUpdate&&) = default;
            MaterialUpdate& operator=(MaterialUpdate&&) = default;
            // 同一帧边界内保存资产后发布；期间不可重建 renderer 或发布 Shader。
            void publish() &&;

        private:
            friend class MaterialRenderer;
            MaterialUpdate() = default;
            MaterialRenderer* m_owner = nullptr;
            AssetHandle m_handle;
            MaterialRuntimeCache m_prepared;
            std::shared_ptr<MaterialResources> m_resources;
        };

        struct Statistics {
            uint32_t draw_calls = 0;
            uint32_t pipeline_binds = 0;
            uint32_t material_binds = 0;
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
            SampleCount samples, const MaterialShaders* shaders = nullptr);
        // 帧边界提交任意完整顶点/片元程序对；所有候选成功后才替换。
        Result<ReloadReport, GraphicsError> reload_shaders(
            PipelineManager& pipelines, const MaterialShaders& shaders, SampleCount samples);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>> get_material_layouts()
            const;
        [[nodiscard]] Result<MaterialUpdate, GraphicsError> prepare_material_update(
            AssetHandle handle, const std::shared_ptr<const Material>& material);
        // 调用方须先等待槽位、开启场景通道并设置视口与裁剪区域。
        // shadow_map 必须已处于片元 SampledRead；即使关闭阴影也需有效采样绑定。
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, const std::optional<ViewProjectMatrix>& view,
            std::span<const ResolvedRenderItem> items, const LightingData& lighting,
            const std::shared_ptr<ImageView>& shadow_map);
        [[nodiscard]] const Statistics& get_statistics() const { return m_statistics; }

    private:
        explicit MaterialRenderer(Device& device);
        Result<void, GraphicsError> initialize(PipelineManager& pipelines,
            RenderResources& resources, uint32_t frame_slot_count, SampleCount samples,
            const MaterialShaders* shaders);

        struct PipelineState {
            std::shared_ptr<const MaterialLayout> layout;
            std::shared_ptr<DescriptorSetLayout> material_layout;
            std::shared_ptr<Pipeline> pipeline;
        };
        struct FrameResources {
            std::shared_ptr<DescriptorSetLayout> layout;
            std::shared_ptr<DescriptorPool> pool;
            std::shared_ptr<CPUBuffer> buffer;
            std::shared_ptr<CPUBuffer> lighting;
            std::shared_ptr<Sampler> shadow_sampler;
            std::shared_ptr<ImageView> shadow_map;
            std::optional<DescriptorSet> descriptor;
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
            std::shared_ptr<const PreparedMaterial> failed_candidate;
            std::weak_ptr<const PipelineState> failed_pipeline;
            uint64_t retry_after_serial = 0;
            std::string preparation_error;
            bool used = false;
        };
        struct DrawItem {
            const ResolvedRenderItem* item;
            std::shared_ptr<MaterialResources> material;
        };

        Result<std::shared_ptr<const PipelineState>, GraphicsError> create_pipeline(
            PipelineManager& pipelines, const std::shared_ptr<Shader>& vertex,
            const std::shared_ptr<Shader>& fragment, std::shared_ptr<const MaterialLayout> layout,
            SampleCount samples, std::shared_ptr<DescriptorSetLayout> material_layout);
        // 成功空值表示本次无可绘制版本；失败表示不能继续当前帧。
        [[nodiscard]] Result<std::shared_ptr<MaterialResources>, GraphicsError> prepare_material(
            const MaterialBinding& material, uint64_t frame_serial);
        Result<std::shared_ptr<MaterialResources>, GraphicsError> create_material(
            const std::shared_ptr<const PreparedMaterial>& prepared,
            const std::shared_ptr<const PipelineState>& pipeline,
            const std::shared_ptr<MaterialResources>& previous);

        Device& m_device;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Texture> m_white_texture;
        std::shared_ptr<DescriptorSetLayout> m_frame_layout;
        std::vector<std::shared_ptr<FrameResources>> m_frames;
        std::unordered_map<std::string, std::shared_ptr<const PipelineState>> m_pipelines;
        MaterialRuntimeCache m_prepared;
        std::unordered_map<AssetHandle, CachedMaterial> m_materials;
        std::unordered_map<AssetHandle, uint64_t> m_unsupported;
        Statistics m_statistics;
    };
}
