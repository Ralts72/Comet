#pragma once

#include "graphics/result.h"
#include "particles/particle_data.h"
#include "render/scene/render_types.h"
#include <memory>
#include <span>
#include <vector>

namespace Comet {
    class Device;
    class Buffer;
    class FrameScheduler;

    // 每 View／帧槽独立输出；重复呈现同一快照不会推进模拟时钟。
    class COMET_API GpuParticleSimulation {
    public:
        struct Output {
            std::shared_ptr<Buffer> instances, indirect;
            uint32_t count = 0;
        };
        GpuParticleSimulation(Device&, uint32_t slots);
        ~GpuParticleSimulation();
        Result<void, GraphicsError> prepare(
            FrameScheduler&, const ViewProjectMatrix&, std::span<const GpuParticleEmitter>);
        [[nodiscard]] std::span<const Output> outputs() const { return m_outputs; }
        void reset();

    private:
        struct Program;
        struct Resources;
        Result<void, GraphicsError> initialize();
        Result<std::shared_ptr<Resources>, GraphicsError> allocate(uint32_t count, size_t batches);
        Device& m_device;
        std::shared_ptr<Program> m_program;
        std::vector<std::vector<std::shared_ptr<Resources>>> m_frames;
        std::vector<Output> m_outputs;
    };
}
