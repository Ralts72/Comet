#pragma once
#include "common/export.h"
#include "graphics/resource/resource_result.h"
#include "graphics/synchronization/gpu_completion_point.h"
#include "render/resource/mesh_data.h"

#include <memory>

namespace Comet {
    class Buffer;
    class CommandBuffer;
    class Device;
    class UploadManager;

    class COMET_API Mesh {
    public:
        ~Mesh();

        [[nodiscard]] static std::shared_ptr<Mesh> create(
            Device& device,
            UploadManager& upload_manager,
            const MeshData& data);
        [[nodiscard]] static GpuResourceResult<std::shared_ptr<Mesh>>
        try_create(
            Device& device,
            UploadManager& upload_manager,
            const MeshData& data,
            bool within_budget);

        void draw(const CommandBuffer& command_buffer) const;
        [[nodiscard]] const GpuCompletionPoint& get_ready_completion() const {
            return m_ready_completion;
        }

    private:
        Mesh(
            std::shared_ptr<Buffer> vertex_buffer,
            std::shared_ptr<Buffer> index_buffer,
            GpuCompletionPoint ready_completion,
            uint32_t vertex_count,
            uint32_t index_count);

        std::shared_ptr<Buffer> m_vertex_buffer;
        std::shared_ptr<Buffer> m_index_buffer;
        GpuCompletionPoint m_ready_completion;
        uint32_t m_vertex_count;
        uint32_t m_index_count;
    };

}
