#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/queue.h"
#include "render/material/material_runtime.h"
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
    class FrameScheduler;
    class Pipeline;
    class PipelineManager;
    class RenderResources;
    class Sampler;
    class Shader;

    // 调用方须先等待槽位、开启场景通道并设置视口与裁剪区域。
    class COMET_API MaterialRenderer {
    public:
        struct ShaderCode {
            std::vector<uint32_t> vertex;
            std::vector<uint32_t> textured_fragment;
            std::vector<uint32_t> solid_fragment;
        };

        struct Statistics {
            uint32_t draw_calls = 0;
            uint32_t pipeline_binds = 0;
            uint32_t material_binds = 0;
            uint32_t material_versions_created = 0;
            uint32_t material_bindings_created = 0;
            uint32_t cached_material_versions = 0;
            uint32_t frame_set_count = 0;
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
            SampleCount samples, const ShaderCode* shaders = nullptr);
        // 在新一帧绘制前调用；两种材质管线全部成功后才替换。
        Result<ReloadReport, GraphicsError> reload_shaders(
            PipelineManager& pipelines, const ShaderCode& shaders, SampleCount samples);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>> get_material_layouts()
            const;
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, const std::optional<ViewProjectMatrix>& view,
            std::span<const ResolvedRenderItem> items);
        [[nodiscard]] const Statistics& get_statistics() const { return m_statistics; }

    private:
        explicit MaterialRenderer(Device& device);
        Result<void, GraphicsError> initialize(PipelineManager& pipelines,
            RenderResources& resources, uint32_t frame_slot_count, SampleCount samples,
            const ShaderCode* shaders);

        struct PipelineState {
            std::shared_ptr<const MaterialLayout> layout;
            std::shared_ptr<DescriptorSetLayout> material_layout;
            std::shared_ptr<Pipeline> pipeline;
        };
        struct FrameResources {
            std::shared_ptr<DescriptorSetLayout> layout;
            std::shared_ptr<DescriptorPool> pool;
            std::shared_ptr<CPUBuffer> buffer;
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
        std::shared_ptr<DescriptorSetLayout> m_frame_layout;
        std::vector<std::shared_ptr<FrameResources>> m_frames;
        std::unordered_map<std::string, std::shared_ptr<const PipelineState>> m_pipelines;
        MaterialRuntimeCache m_prepared;
        std::unordered_map<AssetHandle, CachedMaterial> m_materials;
        std::unordered_map<AssetHandle, uint64_t> m_unsupported;
        Statistics m_statistics;
    };
}
