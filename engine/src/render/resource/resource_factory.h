#pragma once

#include "common/export.h"
#include "graphics/result.h"

#include <memory>

namespace Comet {
    class Mesh;
    class Texture;
    struct MeshData;
    struct TextureData;

    class COMET_API RenderResourceFactory {
    public:
        virtual ~RenderResourceFactory() = default;

        // CPU 产品按值交付，线程工厂可直接移走存储；返回后候选可由调用方注册。
        [[nodiscard]] virtual GpuResourceResult<std::shared_ptr<Texture>> try_create_texture(
            TextureData data) = 0;
        [[nodiscard]] virtual GpuResourceResult<std::shared_ptr<Mesh>> try_create_mesh(
            MeshData data) = 0;
    };
}
