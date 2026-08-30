#include "render/resource/mesh.h"

namespace Comet {
    Mesh::Mesh(Device& device, const MeshData& data)
    : m_vertex_count(data.vertices.size()), m_index_count(data.indices.size()) {
        if(data.vertices.empty()) {
            LOG_FATAL("vertices array is empty, can't create mesh");
        }
        m_vertex_buffer = Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::Vertex),
            data.vertices.size() * sizeof(MeshVertex), data.vertices.data(), "mesh vertex buffer");
        if(!data.indices.empty()) {
            m_index_buffer = Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::Index),
                data.indices.size() * sizeof(uint32_t), data.indices.data(), "mesh index buffer");
        } else {
            m_index_buffer.reset();
        }
    }

    Mesh::~Mesh() {
        m_vertex_buffer.reset();
        m_index_buffer.reset();
    }

    void Mesh::draw(const CommandBuffer& command_buffer) const {
        command_buffer.bind_vertex_buffer({*m_vertex_buffer, 0});

        if(m_index_count > 0) {
            command_buffer.bind_index_buffer(*m_index_buffer, 0, vk::IndexType::eUint32);
            command_buffer.draw_indexed(m_index_count, 1, 0, 0);
        } else {
            command_buffer.draw(m_vertex_count, 1, 0, 0);
        }
    }

}
