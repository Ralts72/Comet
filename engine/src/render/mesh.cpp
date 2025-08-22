#include "mesh.h"

namespace Comet {
    Mesh::Mesh(Device* device, const std::vector<Math::Vertex>& vertices, const std::vector<uint32_t>& indices)
    : m_vertex_count(vertices.size()), m_index_count(indices.size()) {
        if(vertices.empty()) {
            LOG_FATAL("vertices array is empty, can't create mesh");
        }
        m_vertex_buffer = std::make_shared<Buffer>(device, vk::BufferUsageFlagBits::eVertexBuffer,
            vertices.size() * sizeof(Math::Vertex), vertices.data());
        if(!indices.empty()) {
            m_index_buffer = std::make_shared<Buffer>(device, vk::BufferUsageFlagBits::eIndexBuffer,
                indices.size() * sizeof(uint32_t), indices.data());
        } else {
            m_index_buffer.reset();
        }
    }

    Mesh::~Mesh() {
        m_vertex_buffer.reset();
        m_index_buffer.reset();
    }

    void Mesh::draw(CommandBuffer& command_buffer) {
        const Buffer* buffer_array[] = {m_vertex_buffer.get()};
        constexpr vk::DeviceSize offsets[] = {0};
        command_buffer.bind_vertex_buffer(buffer_array, offsets, 0);
        if(m_index_count > 0) {
            command_buffer.bind_index_buffer(*m_index_buffer, 0, vk::IndexType::eUint32);
            command_buffer.draw_indexed(m_index_count, 1, 0, 0);
        } else {
            command_buffer.draw(m_vertex_count, 1, 0, 0);
        }
    }

}
