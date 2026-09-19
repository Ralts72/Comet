#include "assets/material_editing.h"

#include <algorithm>

namespace CometEditor {
    Comet::MaterialData make_material_data(const Comet::MaterialLayout& layout) {
        Comet::MaterialData data{.template_name = layout.get_name()};
        for(const auto& property : layout.get_scalars())
            data.scalar_properties.emplace(property.name, property.default_value);
        for(const auto& property : layout.get_vectors())
            data.vector_properties.emplace(property.name, property.default_value);
        return data;
    }

    MaterialTemplateChange change_material_template(const Comet::MaterialData& data,
        const Comet::MaterialLayout* previous, const Comet::MaterialLayout& next) {
        MaterialTemplateChange change{make_material_data(next), {}};
        const auto copy_compatible = [&](const auto& values, const auto& old_properties,
                                         const auto& new_properties, auto& destination,
                                         const auto& compatible) {
            for(const auto& [name, value] : values) {
                const auto old = std::ranges::find_if(
                    old_properties, [&](const auto& property) { return property.name == name; });
                const auto target = std::ranges::find_if(
                    new_properties, [&](const auto& property) { return property.name == name; });
                if(previous && old != old_properties.end() && target != new_properties.end()
                    && compatible(*old, *target))
                    destination[name] = value;
                else
                    change.discarded_properties.push_back(name);
            }
        };
        // Unknown source templates have no semantic contract; none of their values are guessed.
        const auto& source = previous ? *previous : next;
        copy_compatible(data.texture_properties, source.get_textures(), next.get_textures(),
            change.data.texture_properties, [](const auto&, const auto&) { return true; });
        copy_compatible(data.scalar_properties, source.get_scalars(), next.get_scalars(),
            change.data.scalar_properties, [](const auto&, const auto&) { return true; });
        copy_compatible(data.vector_properties, source.get_vectors(), next.get_vectors(),
            change.data.vector_properties,
            [](const auto& old, const auto& target) { return old.semantic == target.semantic; });
        return change;
    }
}
