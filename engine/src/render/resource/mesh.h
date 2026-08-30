#pragma once
#include "common/export.h"
#include "graphics/resource/buffer.h"
#include "graphics/device.h"
#include "render/resource/mesh_data.h"

namespace Comet {

    class COMET_API Mesh {
    public:
        Mesh(Device& device, const MeshData& data);
        ~Mesh();

        void draw(const CommandBuffer& command_buffer) const;

    private:
        std::shared_ptr<Buffer> m_vertex_buffer;
        std::shared_ptr<Buffer> m_index_buffer;
        uint32_t m_vertex_count;
        uint32_t m_index_count;
    };

}
