#pragma once

#include "common/error.h"
#include "common/export.h"
#include "common/result.h"

#include <map>
#include <memory>
#include <string>

namespace Comet {
    class RenderResourceFactory;
    class COMET_API Mesh;
    class COMET_API Texture;
    class COMET_API Material;
    struct COMET_API Environment;
    struct MeshData;
    struct TextureData;
    struct EnvironmentData;
    struct MaterialData;

    // 接收独立 CPU 产品并返回完整 GPU 候选；不读取文件、索引或 Registry。
    // Main 复核请求后注册；创建与接纳当前仍串行执行。
    class COMET_API RenderAssetFactory final {
    public:
        explicit RenderAssetFactory(RenderResourceFactory& resources);

        [[nodiscard]] Result<std::shared_ptr<Mesh>, Error> prepare(MeshData data);
        [[nodiscard]] Result<std::shared_ptr<Texture>, Error> prepare(TextureData data);
        [[nodiscard]] Result<std::shared_ptr<Environment>, Error> prepare(EnvironmentData data);
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
