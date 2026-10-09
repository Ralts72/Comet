#include "render/resource/instance_buffer.h"

#include "graphics/command/command_buffer.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/resource/buffer.h"

#include <algorithm>

namespace Comet {
    void InstanceBuffer::describe(VertexInputDescription& input) {
        static_assert(sizeof(Math::Mat4) == 4 * sizeof(Math::Vec4));
        input.add_binding(1, sizeof(Math::Mat4), VertexInputRate::Instance);
        for(uint32_t column = 0; column < 4; ++column)
            input.add_attribute(
                3 + column, 1, Format::R32G32B32A32_SFLOAT, column * sizeof(Math::Vec4));
    }

    Result<void, GraphicsError> InstanceBuffer::upload(
        Device& device, const std::span<const Math::Mat4> transforms) {
        if(transforms.empty())
            return Result<void, GraphicsError>::success();
        const auto bytes = transforms.size_bytes();
        if(!m_buffer || m_buffer->get_size() < bytes) {
            const auto capacity =
                std::max(bytes, m_buffer ? m_buffer->get_size() * 2 : size_t{4096});
            auto buffer =
                Buffer::try_create_cpu_buffer(device, Flags<BufferUsage>(BufferUsage::Vertex),
                    capacity, true, nullptr, "mesh instance transforms");
            if(!buffer)
                return Result<void, GraphicsError>::failure(buffer.error());
            m_buffer = std::move(buffer).value();
        }
        m_buffer->write(transforms.data(), bytes, 0);
        return Result<void, GraphicsError>::success();
    }

    void InstanceBuffer::bind(const CommandBuffer& command) const {
        command.bind_vertex_buffer({*m_buffer, 0}, 1);
    }
}
