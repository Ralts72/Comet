#pragma once

#include "common/export.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/queue.h"
#include "render/material_runtime.h"
#include "render/scene/render_submission.h"
#include "graphics/pipeline/shader.h"

#include <optional>
#include <span>

namespace Comet {
    class CPUBuffer;
    class Device;
    class FrameScheduler;
    class Pipeline;
    class PipelineManager;
    class ResourceManager;
    class Sampler;
    class Shader;
    class ImageView;

    // 在已开启的场景 pass 内绘制 Mesh；调用方负责等待 slot 和设置 viewport/scissor。
    class COMET_API MaterialRenderer {
    public:
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
        };

        MaterialRenderer(Device& device, PipelineManager& pipelines,
            ResourceManager& resources, uint32_t frame_slot_count, SampleCount samples);
        [[nodiscard]] std::vector<QueueSemaphoreSubmit> render(FrameScheduler& frames,
            const ViewProjectMatrix& view, std::span<const ResolvedRenderItem> items,
            const LightingData& lighting = {},
            std::shared_ptr<ImageView> shadow_map = {});
        [[nodiscard]] const Statistics& get_statistics() const { return m_statistics; }
        // owner 帧边界调用；Shader、布局和所有驻留材质候选全部成功才发布。
        ReloadReport reload_shaders(PipelineManager& pipelines, ShaderManager& shaders,
            const ShaderManager::Bytecodes& bytecodes, SampleCount samples);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>>
        get_material_layouts() const;

    private:
        struct PipelineState {
            std::shared_ptr<const MaterialLayout> layout;
            std::shared_ptr<DescriptorSetLayout> frame_layout;
            std::shared_ptr<DescriptorSetLayout> material_layout;
            std::shared_ptr<Pipeline> pipeline;
        };
        struct FrameResources {
            std::shared_ptr<DescriptorSetLayout> layout;
            std::shared_ptr<DescriptorPool> pool;
            std::shared_ptr<CPUBuffer> buffer;
            std::shared_ptr<CPUBuffer> lighting;
            std::shared_ptr<ImageView> shadow_map;
            std::shared_ptr<Sampler> shadow_sampler;
            std::optional<DescriptorSet> descriptor;
        };
        struct MaterialResources {
            std::shared_ptr<const PipelineState> pipeline;
            std::shared_ptr<const PreparedMaterial> prepared;
            std::shared_ptr<Sampler> sampler;
            std::shared_ptr<CPUBuffer> parameters;
            std::shared_ptr<DescriptorPool> pool;
            std::optional<DescriptorSet> descriptor;
        };
        struct CachedMaterial {
            std::shared_ptr<MaterialResources> resources;
            std::shared_ptr<const PreparedMaterial> failed_candidate;
            std::shared_ptr<const PipelineState> failed_pipeline;
            uint64_t retry_after_serial = 0;
            bool used = false;
        };
        struct DrawItem {
            const ResolvedRenderItem* item;
            std::shared_ptr<MaterialResources> material;
        };

        [[nodiscard]] std::shared_ptr<const PipelineState> create_pipeline(
            PipelineManager& pipelines, const std::shared_ptr<Shader>& vertex,
            const std::shared_ptr<Shader>& fragment,
            std::shared_ptr<const MaterialLayout> layout, SampleCount samples,
            std::shared_ptr<DescriptorSetLayout> material_layout = {});
        [[nodiscard]] std::shared_ptr<MaterialResources> prepare_material(
            const MaterialBinding& material, uint64_t frame_serial);
        [[nodiscard]] std::shared_ptr<MaterialResources> create_material(
            const std::shared_ptr<const PreparedMaterial>& prepared,
            const std::shared_ptr<const PipelineState>& pipeline,
            const std::shared_ptr<MaterialResources>& previous);

        Device& m_device;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<ImageView> m_fallback_shadow;
        std::shared_ptr<DescriptorSetLayout> m_frame_layout;
        std::vector<std::shared_ptr<FrameResources>> m_frames;
        std::unordered_map<std::string, std::shared_ptr<const PipelineState>> m_pipelines;
        MaterialRuntimeCache m_prepared;
        std::unordered_map<AssetHandle, CachedMaterial> m_materials;
        std::unordered_map<AssetHandle, uint64_t> m_unsupported;
        Statistics m_statistics;
    };
}
