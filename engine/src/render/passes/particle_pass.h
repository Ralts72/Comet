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
    class CPUBuffer;
    class Device;
    class FrameScheduler;
    class Pipeline;
    class PipelineManager;
    class RenderResources;
    class Texture;
    class Sampler;
    class DescriptorSetLayout;
    struct SampledImageBinding;
    struct RenderWorld;

    class COMET_API ParticlePass {
    public:
        static Result<std::unique_ptr<ParticlePass>, GraphicsError> create(Device& device,
            PipelineManager& pipelines, RenderResources& resources, uint32_t frame_slots,
            SampleCount samples);
        Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, const ViewProjectMatrix& camera, const RenderWorld& world);

        void reset() { m_bindings.clear(); }

    private:
        struct DrawBatch {
            uint32_t first, count, source;
        };
        struct SortEntry {
            uint32_t instance, batch;
            float depth;
        };
        ParticlePass(Device& device, uint32_t frame_slots);
        Result<void, GraphicsError> prepare_instances(const ParticleSnapshot& particles,
            std::span<const ParticleSnapshot::Batch> batches, const Math::Mat4& view);
        Device& m_device;
        std::array<std::shared_ptr<Pipeline>, 2> m_pipelines;
        std::vector<SortEntry> m_order;
        std::vector<ParticleInstance> m_sorted_instances;
        std::vector<DrawBatch> m_draw_batches;
        std::vector<std::shared_ptr<CPUBuffer>> m_buffers;
        std::shared_ptr<Texture> m_white;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::unordered_map<AssetHandle, std::shared_ptr<SampledImageBinding>> m_bindings;
    };
}
