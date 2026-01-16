#pragma once
#include "graphics/buffer.h"
#include "common/geometry_utils.h"
#include "graphics/device.h"

namespace Comet {

    class Mesh {
    public:
        Mesh(Device* device, const std::vector<Math::Vertex>& vertices, const std::vector<uint32_t>& indices = {});
        ~Mesh();

        void draw(const CommandBuffer& command_buffer) const;

    private:
        std::shared_ptr<Buffer> m_vertex_buffer;
        std::shared_ptr<Buffer> m_index_buffer;
        uint32_t m_vertex_count;
        uint32_t m_index_count;
    };

}
