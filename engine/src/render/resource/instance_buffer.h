#pragma once

#include "core/math_utils.h"
#include "graphics/result.h"

#include <memory>
#include <span>
#include <vector>

namespace Comet {
    class CPUBuffer;
    class CommandBuffer;
    class Device;
    class VertexInputDescription;

    // 每个飞行帧槽位独占；槽位回收后才能再次上传。
    class InstanceBuffer {
    public:
        static void describe(
            VertexInputDescription& input, uint32_t binding = 1, uint32_t first_location = 3);
        Result<size_t, GraphicsError> upload(
            Device& device, std::span<const Math::Mat4> transforms);
        void bind(const CommandBuffer& command, uint32_t binding = 1) const;
        [[nodiscard]] const std::shared_ptr<CPUBuffer>& get_buffer() const { return m_buffer; }

    private:
        std::shared_ptr<CPUBuffer> m_buffer;
        std::vector<Math::Mat4> m_uploaded_transforms;
    };
}
