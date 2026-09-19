#pragma once

#include "asset/data/material_data.h"
#include "render/material/material_layout.h"

namespace Comet {
    class AssetDatabase;
}

namespace CometEditor {
    struct MaterialTemplateChange {
        Comet::MaterialData data;
        std::vector<std::string> discarded_properties;
    };

    [[nodiscard]] Comet::MaterialData make_material_data(const Comet::MaterialLayout& layout);
    [[nodiscard]] Comet::Result<void> validate_material_data(const Comet::MaterialData& data,
        const Comet::MaterialLayout& layout, const Comet::AssetDatabase& database);
    [[nodiscard]] MaterialTemplateChange change_material_template(const Comet::MaterialData& data,
        const Comet::MaterialLayout* previous, const Comet::MaterialLayout& next);
}
