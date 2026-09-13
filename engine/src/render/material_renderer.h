#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/queue.h"
#include "render/material_runtime.h"
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
    class ResourceManager;
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
            uint32_t cached_material_versions = 0;
            uint32_t frame_set_count = 0;
        };

        static Result<std::unique_ptr<MaterialRenderer>, GraphicsError> create(Device& device,
            PipelineManager& pipelines, ResourceManager& resources, uint32_t frame_slot_count,
            SampleCount samples, const ShaderCode* shaders = nullptr);
        // 在新一帧绘制前调用；两种材质管线全部成功后才替换。
        Result<void, GraphicsError> reload_shaders(
            PipelineManager& pipelines, const ShaderCode& shaders, SampleCount samples);
        [[nodiscard]] std::vector<QueueSemaphoreSubmit> render(FrameScheduler& frames,
            const ViewProjectMatrix& view, std::span<const ResolvedRenderItem> items);
        [[nodiscard]] const Statistics& get_statistics() const { return m_statistics; }

    private:
        explicit MaterialRenderer(Device& device);
        Result<void, GraphicsError> initialize(PipelineManager& pipelines,
            ResourceManager& resources, uint32_t frame_slot_count, SampleCount samples,
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
            uint64_t retry_after_serial = 0;
            bool used = false;
        };
        struct DrawItem {
            const ResolvedRenderItem* item;
            std::shared_ptr<MaterialResources> material;
        };

        Result<std::shared_ptr<const PipelineState>, GraphicsError> create_pipeline(
            PipelineManager& pipelines, const std::shared_ptr<Shader>& vertex,
            const std::shared_ptr<Shader>& fragment, std::shared_ptr<const MaterialLayout> layout,
            SampleCount samples);
        [[nodiscard]] std::shared_ptr<MaterialResources> prepare_material(
            const MaterialBinding& material, uint64_t frame_serial);

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
