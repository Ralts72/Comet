#pragma once
#include "common/export.h"
#include "graphics/result.h"
#include "graphics/synchronization/gpu_completion_point.h"
#include "asset/data/mesh_data.h"

#include <memory>

namespace Comet {
    class Buffer;
    class CommandBuffer;
    class Device;
    class UploadManager;

    class COMET_API Mesh {
    public:
        ~Mesh() = default;

        [[nodiscard]] static GpuResourceResult<std::shared_ptr<Mesh>> try_create(Device& device,
            UploadManager& upload_manager, const MeshData& data, bool within_budget);

        void bind(const CommandBuffer& command_buffer) const;
        // 当前 CommandBuffer 已绑定此 Mesh 的顶点／索引缓冲。
        void draw(const CommandBuffer& command_buffer, uint32_t instance_count = 1,
            uint32_t first_instance = 0) const;
        [[nodiscard]] const GpuCompletionPoint& get_ready_completion() const {
            return m_ready_completion;
        }
        [[nodiscard]] const BoundingBox& get_local_bounds() const { return m_local_bounds; }

    private:
        Mesh(std::shared_ptr<Buffer> vertex_buffer, std::shared_ptr<Buffer> index_buffer,
            GpuCompletionPoint ready_completion, BoundingBox local_bounds, uint32_t vertex_count,
            uint32_t index_count);

        std::shared_ptr<Buffer> m_vertex_buffer;
        std::shared_ptr<Buffer> m_index_buffer;
        GpuCompletionPoint m_ready_completion;
        BoundingBox m_local_bounds;
        uint32_t m_vertex_count;
        uint32_t m_index_count;
    };

}
