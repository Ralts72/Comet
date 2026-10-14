#pragma once

#include "common/export.h"
#include "graphics/enums.h"
#include "graphics/result.h"
#include "particles/particle_data.h"
#include "render/scene/render_types.h"

#include <memory>
#include <vector>

namespace Comet {
    class CPUBuffer;
    class Device;
    class FrameScheduler;
    class Pipeline;
    class PipelineManager;

    class COMET_API ParticlePass {
    public:
        static Result<std::unique_ptr<ParticlePass>, GraphicsError> create(
            Device& device, PipelineManager& pipelines, uint32_t frame_slots, SampleCount samples);
        Result<void, GraphicsError> render(FrameScheduler& frames, const ViewProjectMatrix& camera,
            const ParticleSnapshot& particles);

    private:
        ParticlePass(Device& device, std::shared_ptr<Pipeline> pipeline, uint32_t frame_slots);
        Device& m_device;
        std::shared_ptr<Pipeline> m_pipeline;
        std::vector<std::shared_ptr<CPUBuffer>> m_buffers;
    };
}
