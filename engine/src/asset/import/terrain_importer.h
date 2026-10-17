#pragma once
#include "asset/import/model_importer.h"
#include "asset/data/terrain_data.h"

namespace Comet {
    // 开发源在导入阶段展开；运行时只消费普通模型、纹理和实例。
    class COMET_API TerrainImporter final {
    public:
        // 调用方提供已展开、通过校验的高度网格；不覆盖已有手绘权重。
        static void initialize_layer_weights(TerrainData&);
        [[nodiscard]] static Result<TerrainData> load_heightfield(
            const std::filesystem::path& source, const AssetImportLimits& limits = {});
        [[nodiscard]] static Result<ModelImporter::SourceInfo> inspect_source(
            const std::filesystem::path& source, const AssetImportLimits& limits = {});
        [[nodiscard]] Result<ModelImportData> import(const std::filesystem::path& source,
            std::size_t memory_budget, const AssetImportLimits& limits = {}) const;
    };
}
