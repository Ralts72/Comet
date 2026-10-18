#pragma once

#include "asset/runtime/product_reader.h"
#include "asset/import/import_candidate.h"

namespace Comet {
    class AssetDatabase;
    class ImportService;
    [[nodiscard]] std::unique_ptr<AssetProductReader> make_project_product_reader(
        AssetDatabase&, ImportService&, AssetImportLimits limits);
}
