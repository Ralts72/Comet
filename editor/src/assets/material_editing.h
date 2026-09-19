#pragma once

#include "asset/material_data.h"
#include "render/material/material_layout.h"

namespace CometEditor {
    struct MaterialTemplateChange {
        Comet::MaterialData data;
        std::vector<std::string> discarded_properties;
    };

    [[nodiscard]] Comet::MaterialData make_material_data(const Comet::MaterialLayout& layout);
    [[nodiscard]] MaterialTemplateChange change_material_template(const Comet::MaterialData& data,
        const Comet::MaterialLayout* previous, const Comet::MaterialLayout& next);
}
