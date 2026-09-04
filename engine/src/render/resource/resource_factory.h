#pragma once

#include "common/export.h"
#include "render/resource/mesh_data.h"
#include "render/resource/texture_data.h"

#include <memory>

namespace Comet {
    class Mesh;
    class Texture;

    class COMET_API RenderResourceFactory {
    public:
        virtual ~RenderResourceFactory() = default;

        [[nodiscard]] virtual std::shared_ptr<Texture> create_texture(
            const TextureData& data) = 0;
        [[nodiscard]] virtual std::shared_ptr<Mesh> create_mesh(const MeshData& data) = 0;
    };
}
