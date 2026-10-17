#pragma once
#include "asset/import/model_importer.h"

namespace Comet {
    // 开发源在导入阶段展开；运行时只消费普通模型、纹理和实例。
    class COMET_API TerrainImporter final {
    public:
        [[nodiscard]] static Result<ModelImporter::SourceInfo> inspect_source(
            const std::filesystem::path& source, const AssetImportLimits& limits = {});
        [[nodiscard]] Result<ModelImportData> import(const std::filesystem::path& source,
            std::size_t memory_budget, const AssetImportLimits& limits = {}) const;
    };
}
