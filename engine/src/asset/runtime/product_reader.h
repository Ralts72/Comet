#pragma once

#include "asset/artifact/model_artifact.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"
#include "asset/data/environment_data.h"
#include "common/error.h"

namespace Comet {
    struct AssetRecord;
    struct TextureImportSettings;
    class ShaderProgramArtifact;
    // Loader 只请求可消费的 CPU 产品；来源决定如何准备及验证开发产品。
    class COMET_API AssetProductReader {
    public:
        virtual ~AssetProductReader() = default;
        [[nodiscard]] virtual Result<std::filesystem::path, Error> location(const AssetRecord&) = 0;
        [[nodiscard]] virtual std::optional<ModelArtifact> model(const AssetRecord&) = 0;
        [[nodiscard]] virtual Result<MeshData, Error> default_character() = 0;
        [[nodiscard]] virtual Result<TextureData, Error> texture(
            const AssetRecord&, const TextureImportSettings* settings, bool prepare) = 0;
        [[nodiscard]] virtual Result<EnvironmentData, Error> environment(
            const AssetRecord&, bool prepare) = 0;
        [[nodiscard]] virtual bool shader_current(const ShaderProgramArtifact&) const = 0;
    };
}
