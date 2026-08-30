#pragma once
#include "common/export.h"
#include "graphics/queue.h"
#include "graphics/resource/buffer.h"
#include "graphics/device.h"
#include "render/resource/mesh_data.h"

namespace Comet {
    class UploadManager;

    class COMET_API Mesh {
    public:
        Mesh(Device& device, UploadManager& upload_manager, const MeshData& data);
        ~Mesh();

        void draw(const CommandBuffer& command_buffer) const;
        [[nodiscard]] const GpuCompletionPoint& get_ready_completion() const {
            return m_ready_completion;
        }

    private:
        std::shared_ptr<Buffer> m_vertex_buffer;
        std::shared_ptr<Buffer> m_index_buffer;
        GpuCompletionPoint m_ready_completion;
        uint32_t m_vertex_count;
        uint32_t m_index_count;
    };

}
