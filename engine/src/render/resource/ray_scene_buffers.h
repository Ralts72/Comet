#pragma once

#include "graphics/result.h"
#include "render/ray_scene.h"

#include <cstddef>

namespace Comet {
    class Device;
    class CPUBuffer;
    class DescriptorSet;

    // BVH、材质和光源的只读 GPU 代际；路径追踪与实时探针共用上传及绑定协议。
    struct COMET_API RaySceneBuffers {
        std::shared_ptr<const RayScene> source;
        std::array<std::shared_ptr<CPUBuffer>, 8> buffers;
        std::array<size_t, 8> sizes{};
        uint64_t upload_bytes = 0;
        uint64_t geometry_upload_bytes = 0;

        [[nodiscard]] static Result<std::shared_ptr<RaySceneBuffers>, GraphicsError> prepare(
            Device& device, const std::shared_ptr<const RayScene>& scene,
            const std::shared_ptr<RaySceneBuffers>& previous = {});
        void write(Device& device, const DescriptorSet& descriptor) const;
    };
}
