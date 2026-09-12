#pragma once

#include "common/export.h"
#include "graphics/resource/resource_result.h"

#include <memory>

namespace Comet {
    class Mesh;
    class Texture;
    struct MeshData;
    struct TextureData;

    class COMET_API RenderResourceFactory {
    public:
        virtual ~RenderResourceFactory() = default;

        [[nodiscard]] virtual GpuResourceResult<std::shared_ptr<Texture>>
        try_create_texture(const TextureData& data) = 0;
        [[nodiscard]] virtual GpuResourceResult<std::shared_ptr<Mesh>> try_create_mesh(
            const MeshData& data) = 0;
    };
}
