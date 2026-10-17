#pragma once

#include "common/export.h"
#include "graphics/enums.h"
#include "graphics/result.h"
#include "graphics/queue.h"
#include "particles/particle_data.h"
#include "render/scene/render_types.h"

#include <memory>
#include <array>
#include <span>
#include <vector>
#include <unordered_map>

namespace Comet {
    struct FogVolume;
    class CPUBuffer;
    class Buffer;
    class GpuParticleSimulation;
    class DescriptorSet;
    class TransparencyPass;
    class Device;
    class FrameScheduler;
    class Pipeline;
    class PipelineManager;
    class RenderResources;
    class Texture;
    class Sampler;
    class ImageView;
    class DescriptorSetLayout;
    struct SampledImageBinding;
    struct RenderWorld;

    class COMET_API ParticlePass {
    public:
        ~ParticlePass();
        static Result<std::unique_ptr<ParticlePass>, GraphicsError> create(Device& device,
            PipelineManager& pipelines, RenderResources& resources, uint32_t frame_slots,
            SampleCount samples, TransparencyPass* transparency = nullptr);
        Result<void, GraphicsError> prepare_gpu(FrameScheduler&, const ViewProjectMatrix&,
            const RenderWorld&, bool sorted = true,
            const std::shared_ptr<ImageView>& depth = nullptr);
        Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> prepare(FrameScheduler& frames,
            const ViewProjectMatrix& camera, const RenderWorld& world, bool sort_for_meshes,
            std::shared_ptr<ImageView> scene_depth = {}, const FogVolume* fog = nullptr,
            bool weighted = false);
        [[nodiscard]] std::span<const float> depths() const { return m_depths; }
        // 透明队列条目数：CPU 粒子各一项，GPU 发射器各一项（其内部由 GPU 排序）。
        [[nodiscard]] uint32_t queue_size() const {
            if(m_draw_batches.empty())
                return 0;
            const auto& last = m_draw_batches.back();
            return last.first + last.count;
        }
        void record(FrameScheduler& frames, uint32_t first, uint32_t count,
            const DescriptorSet* depth = nullptr);
        void clear_draws();

        void reset();

    private:
        struct BillboardCamera {
            Math::Mat4 view_projection;
            Math::Vec4 right, up;
        };
        struct DrawSource {
            std::shared_ptr<SampledImageBinding> binding;
            ParticleBlend blend;
            bool textured;
            float soft_distance;
            std::shared_ptr<Buffer> instances, indirect;
        };
        struct DrawBatch {
            uint32_t first, count, source;
            uint32_t buffer_first = 0;
            bool gpu = false;
        };
        struct SortEntry {
            uint32_t instance, batch;
            float depth;
            bool gpu = false;
        };
        ParticlePass(Device& device, uint32_t frame_slots);
        Result<void, GraphicsError> prepare_instances(const ParticleSnapshot& particles,
            std::span<const ParticleSnapshot::Batch> batches, const Math::Mat4& view, bool ordered,
            bool weighted);
        Device& m_device;
        std::unique_ptr<GpuParticleSimulation> m_gpu;
        std::array<std::shared_ptr<Pipeline>, 2> m_pipelines, m_weighted_pipelines;
        std::vector<SortEntry> m_order;
        std::vector<ParticleInstance> m_sorted_instances;
        std::vector<DrawBatch> m_draw_batches;
        std::vector<DrawSource> m_sources;
        std::vector<float> m_depths;
        BillboardCamera m_camera;
        Math::Vec4 m_depth_coefficients{};
        std::vector<std::shared_ptr<CPUBuffer>> m_buffers;
        std::shared_ptr<Texture> m_white;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<DescriptorSetLayout> m_depth_layout;
        std::shared_ptr<Sampler> m_depth_sampler;
        std::vector<std::shared_ptr<SampledImageBinding>> m_depth_bindings;
        std::unordered_map<AssetHandle, std::shared_ptr<SampledImageBinding>> m_bindings;
    };
}
