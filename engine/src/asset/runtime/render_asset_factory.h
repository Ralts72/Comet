#pragma once

#include "asset/content_fwd.h"

#include "common/error.h"
#include "common/result.h"

#include <map>
#include <memory>
#include <string>

namespace Comet {
    class RenderResourceFactory;
    struct Environment;
    struct MeshData;
    struct TextureData;
    struct EnvironmentData;
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
        [[nodiscard]] Result<std::shared_ptr<VectorField>, Error> prepare(VectorFieldData data);
        [[nodiscard]] Result<std::shared_ptr<Environment>, Error> prepare_preview(
            TextureData background);
        [[nodiscard]] static Result<std::shared_ptr<Material>, Error> prepare_material(
            std::string name, const MaterialData& data,
            const std::map<std::string, std::shared_ptr<Texture>>& textures);

        [[nodiscard]] static bool has_lighting(const std::shared_ptr<Environment>& environment);

    private:
        RenderResourceFactory& m_resources;
    };
}
