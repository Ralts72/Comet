#include "render/resource/mesh.h"

#include "diagnostics/logger.h"
#include "graphics/command/command_buffer.h"
#include "graphics/command/upload_manager.h"
#include "graphics/resource/buffer.h"

#include <limits>
#include <span>

namespace Comet {
    std::shared_ptr<Mesh> Mesh::create(
        Device& device,
        UploadManager& upload_manager,
        const MeshData& data) {
        auto attempt = try_create(device, upload_manager, data, false);
        if(!attempt) {
            LOG_FATAL("Failed to create mesh: {}",
                vk::to_string(attempt.result()));
        }
        return std::move(attempt).value();
    }

    GpuResourceResult<std::shared_ptr<Mesh>> Mesh::try_create(
        Device& device,
        UploadManager& upload_manager,
        const MeshData& data,
        const bool within_budget) {
        if(data.vertices.empty()) {
            LOG_FATAL("vertices array is empty, can't create mesh");
        }
        if(data.vertices.size() > std::numeric_limits<uint32_t>::max()
            || data.indices.size() > std::numeric_limits<uint32_t>::max()) {
            LOG_FATAL("Mesh vertex or index count exceeds uint32_t range");
        }

        const auto vertex_state = resolve_resource_state(
            ResourceUsage::VertexBuffer);
        if(!vertex_state) {
            LOG_FATAL("Failed to resolve mesh vertex buffer state");
        }
        const auto index_state = resolve_resource_state(
            ResourceUsage::IndexBuffer);
        if(!index_state) {
            LOG_FATAL("Failed to resolve mesh index buffer state");
        }

        auto vertex_attempt = Buffer::try_create_gpu_buffer(
            device,
            Flags<BufferUsage>(BufferUsage::Vertex),
            data.vertices.size() * sizeof(MeshVertex),
            within_budget,
            "mesh vertex buffer");
        if(!vertex_attempt) {
            return GpuResourceResult<std::shared_ptr<Mesh>>::failure(
                vertex_attempt.result());
        }

        auto vertex_buffer = std::move(vertex_attempt).value();
        std::shared_ptr<Buffer> index_buffer;
        if(!data.indices.empty()) {
            auto index_attempt = Buffer::try_create_gpu_buffer(
                device,
                Flags<BufferUsage>(BufferUsage::Index),
                data.indices.size() * sizeof(uint32_t),
                within_budget,
                "mesh index buffer");
            if(!index_attempt) {
                return GpuResourceResult<std::shared_ptr<Mesh>>::failure(
                    index_attempt.result());
            }
            index_buffer = std::move(index_attempt).value();
        }

        auto upload_batch = upload_manager.begin_batch();
        auto vertex_upload = upload_batch.try_enqueue_upload(
            vertex_buffer,
            std::as_bytes(std::span(data.vertices)),
            *vertex_state,
            within_budget);
        if(!vertex_upload) {
            return GpuResourceResult<std::shared_ptr<Mesh>>::failure(
                vertex_upload.result());
        }
        if(index_buffer) {
            auto index_upload = upload_batch.try_enqueue_upload(
                index_buffer,
                std::as_bytes(std::span(data.indices)),
                *index_state,
                within_budget);
            if(!index_upload) {
                return GpuResourceResult<std::shared_ptr<Mesh>>::failure(
                    index_upload.result());
            }
        }

        const GpuCompletionPoint completion = upload_batch.submit();
        std::shared_ptr<Mesh> mesh(new Mesh(
            std::move(vertex_buffer),
            std::move(index_buffer),
            completion,
            static_cast<uint32_t>(data.vertices.size()),
            static_cast<uint32_t>(data.indices.size())));
        return GpuResourceResult<std::shared_ptr<Mesh>>::success(
            std::move(mesh));
    }

    Mesh::Mesh(
        std::shared_ptr<Buffer> vertex_buffer,
        std::shared_ptr<Buffer> index_buffer,
        const GpuCompletionPoint ready_completion,
        const uint32_t vertex_count,
        const uint32_t index_count)
        : m_vertex_buffer(std::move(vertex_buffer)),
          m_index_buffer(std::move(index_buffer)),
          m_ready_completion(ready_completion),
          m_vertex_count(vertex_count),
          m_index_count(index_count) {}

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
