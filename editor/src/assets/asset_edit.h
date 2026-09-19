#pragma once

#include "asset/handle.h"
#include "asset/import_settings.h"
#include "asset/data/material_data.h"

#include <variant>

namespace CometEditor {
    struct AssetRead {
        Comet::AssetHandle handle;
        Comet::AssetRevision revision;
    };

    struct MaterialEdit {
        Comet::MaterialData before;
        Comet::MaterialData after;
    };

    struct TextureEdit {
        Comet::TextureImportSettings before;
        Comet::TextureImportSettings after;
    };

    struct AssetEdit {
        Comet::AssetHandle handle;
        Comet::AssetRevision revision;
        std::variant<MaterialEdit, TextureEdit> value;
    };
}
