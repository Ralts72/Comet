#pragma once

#include "core/math_utils.h"
#include "graphics/result.h"

#include <memory>
#include <span>

namespace Comet {
    class CPUBuffer;
    class CommandBuffer;
    class Device;
    class VertexInputDescription;

    // 每个飞行帧槽位独占；槽位回收后才能再次上传。
    class InstanceBuffer {
    public:
        static void describe(VertexInputDescription& input);
        Result<void, GraphicsError> upload(Device& device, std::span<const Math::Mat4> transforms);
        void bind(const CommandBuffer& command) const;
        [[nodiscard]] const std::shared_ptr<CPUBuffer>& get_buffer() const { return m_buffer; }

    private:
        std::shared_ptr<CPUBuffer> m_buffer;
    };
}
