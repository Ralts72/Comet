#pragma once

#include "common/error.h"
#include "common/export.h"
#include "common/result.h"

#include <map>
#include <memory>
#include <string>
#include <cstddef>
#include <vector>

namespace Comet {
    class RenderResourceFactory;
    class COMET_API Mesh;
    class COMET_API Texture;
    class COMET_API Material;
    struct Environment;
    struct COMET_API Irradiance;
    struct MeshData;
    struct TextureData;
    struct EnvironmentData;
    struct COMET_API IrradianceData;
    struct MaterialData;

    // 接收独立 CPU 产品并返回完整 GPU 候选；不读取文件、索引或 Registry。
    // Main 复核请求后注册；GPU 创建由所注入的资源工厂决定执行线程。
    class COMET_API RenderAssetFactory final {
    public:
        explicit RenderAssetFactory(RenderResourceFactory& resources);

        [[nodiscard]] Result<std::shared_ptr<Mesh>, Error> prepare(MeshData data);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> prepare(TextureData data);
        [[nodiscard]] Result<std::shared_ptr<Environment>, Error> prepare(EnvironmentData data);
        [[nodiscard]] Result<std::shared_ptr<Irradiance>, Error> prepare(IrradianceData data);
        [[nodiscard]] Result<std::shared_ptr<Environment>, Error> prepare_preview(
            TextureData background);
        [[nodiscard]] static Result<std::shared_ptr<Material>, Error> prepare_material(
            std::string name, const MaterialData& data,
            const std::map<std::string, std::shared_ptr<Texture>>& textures);

        [[nodiscard]] static bool has_lighting(const std::shared_ptr<Environment>& environment);
        struct ResidentBytes {
            std::size_t mesh_buffer_bytes = 0;
            std::size_t texture_pixel_bytes = 0;
        };
        struct ResidencyInput {
            std::vector<std::shared_ptr<Mesh>> meshes;
            std::vector<std::shared_ptr<Texture>> textures;
            std::vector<std::shared_ptr<Environment>> environments;
            std::vector<std::shared_ptr<Irradiance>> irradiance;
        };
        // 只读去重统计逻辑 payload；不含分配器保留块、暂存或旧帧的独占资源。
        [[nodiscard]] static ResidentBytes resident_bytes(const ResidencyInput& input);

    private:
        RenderResourceFactory& m_resources;
    };
}
