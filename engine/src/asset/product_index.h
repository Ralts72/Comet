#pragma once

#include "asset/metadata.h"
#include "common/result.h"
#include "graphics/enums.h"

namespace Comet {
    struct AssetProduct {
        AssetHandle handle;
        AssetType type = AssetType::Unknown;
        // 相对导出项目 assets/ 的运行文件；不包含源路径或编辑器导入关系。
        std::filesystem::path path;
        std::vector<AssetHandle> dependencies;
        std::optional<SamplerDesc> sampler;
        bool audio_stream = false;
    };

    // 导出时形成依赖闭包，启动时一次性读取，运行期间不扫描或修改。
    class COMET_API ProductIndex final {
    public:
        static constexpr std::string_view FILENAME = "assets.index.json";
        std::vector<AssetProduct> assets;

        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] Result<void> save(const std::filesystem::path& path) const;
        [[nodiscard]] static Result<ProductIndex> load(const std::filesystem::path& path);
    };
}
