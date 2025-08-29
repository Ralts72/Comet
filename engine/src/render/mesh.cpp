#include "mesh.h"

namespace Comet {
    Mesh::Mesh(Device* device, const std::vector<Math::Vertex>& vertices, const std::vector<uint32_t>& indices)
    : m_vertex_count(vertices.size()), m_index_count(indices.size()) {
        if(vertices.empty()) {
            LOG_FATAL("vertices array is empty, can't create mesh");
        }
        m_vertex_buffer = Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::Vertex),
            vertices.size() * sizeof(Math::Vertex), vertices.data());
        if(!indices.empty()) {
            m_index_buffer = Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::Index),
                indices.size() * sizeof(uint32_t), indices.data());
        } else {
            m_index_buffer.reset();
        }
    }

    Mesh::~Mesh() {
        m_vertex_buffer.reset();
        m_index_buffer.reset();
    }

    void Mesh::draw(const CommandBuffer& command_buffer) const {
        const Buffer* buffer = m_vertex_buffer.get();
        uint64_t offset = 0;

        command_buffer.bind_vertex_buffer(
            std::span(&buffer, 1),
            std::span(&offset, 1),
            0
        );

        if(m_index_count > 0) {
            command_buffer.bind_index_buffer(*m_index_buffer, 0, vk::IndexType::eUint32);
            command_buffer.draw_indexed(m_index_count, 1, 0, 0);
        } else {
            command_buffer.draw(m_vertex_count, 1, 0, 0);
        }
    }

}
