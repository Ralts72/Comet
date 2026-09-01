#include "render/resource/mesh.h"

#include "graphics/command/command_buffer.h"
#include "graphics/command/upload_manager.h"
#include "graphics/resource/buffer.h"

#include <span>

namespace Comet {
    Mesh::Mesh(
        Device& device,
        UploadManager& upload_manager,
        const MeshData& data)
    : m_vertex_count(data.vertices.size()), m_index_count(data.indices.size()) {
        if(data.vertices.empty()) {
            LOG_FATAL("vertices array is empty, can't create mesh");
        }
        m_vertex_buffer = Buffer::create_gpu_buffer(
            device,
            Flags<BufferUsage>(BufferUsage::Vertex),
            data.vertices.size() * sizeof(MeshVertex),
            "mesh vertex buffer");
        const auto vertex_state = resolve_resource_state(
            ResourceUsage::VertexBuffer);
        if(!vertex_state) {
            LOG_FATAL("Failed to resolve mesh vertex buffer state");
        }
        auto upload_batch = upload_manager.begin_batch();
        upload_batch.enqueue_upload(
            m_vertex_buffer,
            std::as_bytes(std::span(data.vertices)),
            *vertex_state);
        if(!data.indices.empty()) {
            m_index_buffer = Buffer::create_gpu_buffer(
                device,
                Flags<BufferUsage>(BufferUsage::Index),
                data.indices.size() * sizeof(uint32_t),
                "mesh index buffer");
            const auto index_state = resolve_resource_state(
                ResourceUsage::IndexBuffer);
            if(!index_state) {
                LOG_FATAL("Failed to resolve mesh index buffer state");
            }
            upload_batch.enqueue_upload(
                m_index_buffer,
                std::as_bytes(std::span(data.indices)),
                *index_state);
        } else {
            m_index_buffer.reset();
        }
        m_ready_completion = upload_batch.submit();
    }


    void Mesh::draw(const CommandBuffer& command_buffer) const {
        command_buffer.bind_vertex_buffer({*m_vertex_buffer, 0});

        if(m_index_count > 0) {
            command_buffer.bind_index_buffer(*m_index_buffer, 0, IndexType::Uint32);
            command_buffer.draw_indexed(m_index_count, 1, 0, 0);
        } else {
            command_buffer.draw(m_vertex_count, 1, 0, 0);
        }
    }

}
