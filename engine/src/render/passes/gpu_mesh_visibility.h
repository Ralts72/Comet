#pragma once
#include "core/geometry.h"
#include "graphics/result.h"
#include <memory>
#include <span>
#include <vector>

namespace Comet {
    class Device;
    class Buffer;
    class FrameScheduler;
    // 不透明实例分组各自压缩；保持材质／网格分组及透明排序的现有边界。
    class COMET_API GpuMeshVisibility {
    public:
        struct Batch {
            BoundingBox bounds;
            uint32_t first, count, elements;
        };
        GpuMeshVisibility(Device&, uint32_t slots);
        ~GpuMeshVisibility();
        Result<void, GraphicsError> prepare(FrameScheduler&, const Math::Mat4& clip,
            const std::shared_ptr<Buffer>& source, std::span<const Batch>, uint32_t count);
        [[nodiscard]] std::shared_ptr<Buffer> instances(uint32_t slot) const;
        [[nodiscard]] std::shared_ptr<Buffer> commands(uint32_t slot) const;

    private:
        struct Program;
        struct Resources;
        Result<void, GraphicsError> initialize();
        Device& m_device;
        std::shared_ptr<Program> m_program;
        std::vector<std::shared_ptr<Resources>> m_frames;
    };
}
