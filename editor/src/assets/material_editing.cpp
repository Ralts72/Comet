#include "assets/material_editing.h"
#include "asset/database.h"
#include "assets/editor_assets.h"
#include "render/renderer.h"

#include <algorithm>
#include <utility>

namespace CometEditor {
    Comet::Result<void, Comet::Error> apply_material_edit(
        EditorAssets& assets, Comet::Renderer& renderer, const AssetEdit& edit) {
        auto update = assets.prepare_material_edit(edit);
        if(!update)
            return Comet::Result<void, Comet::Error>::failure(update.error());
        auto bindings = renderer.prepare_material_update(edit.handle, update.value().material());
        if(!bindings)
            return Comet::Result<void, Comet::Error>::failure(bindings.error().as_error());
        if(auto committed = assets.commit_material_edit(update.value()); !committed)
            return committed;
        std::move(bindings).value().publish();
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void> validate_material_data(const Comet::MaterialData& data,
        const Comet::MaterialLayout& layout, const Comet::AssetDatabase& database) {
        if(data.template_name != layout.get_name())
            return Comet::Result<void>::failure("Material template does not match its layout");
        if(data.shader_program) {
            const auto* program = database.find(data.shader_program);
            if(!program || program->type != Comet::AssetType::ShaderProgram)
                return Comet::Result<void>::failure(
                    "Shader program references a missing or non-program asset");
        }
        const auto unknown_property = [](const auto& values, const auto& properties) {
            for(const auto& [name, value] : values) {
                if(!std::ranges::any_of(
                       properties, [&](const auto& property) { return property.name == name; }))
                    return name;
            }
            return std::string{};
        };
        for(const auto& name : {unknown_property(data.texture_properties, layout.get_textures()),
                unknown_property(data.scalar_properties, layout.get_scalars()),
                unknown_property(data.vector_properties, layout.get_vectors())}) {
            if(!name.empty())
                return Comet::Result<void>::failure(
                    "Unknown or incorrectly typed property '" + name + "' in this layout");
        }
        for(const auto& property : layout.get_textures()) {
            if(!property.optional && !data.texture_properties.contains(property.name))
                return Comet::Result<void>::failure(
                    "Complete texture slot '" + property.name + "' to publish changes");
        }

        for(const auto& [property_name, texture_handle] : data.texture_properties) {
            const Comet::AssetRecord* texture = database.find(texture_handle);
            if(!texture) {
                return Comet::Result<void>::failure(
                    "Texture property '" + property_name + "' references a missing asset");
            }
            if(texture->type != Comet::AssetType::Texture) {
                return Comet::Result<void>::failure(
                    "Texture property '" + property_name + "' references a non-texture asset");
            }
        }
        return Comet::Result<void>::success();
    }

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
        // 未知源模板没有可比较的语义，不能猜测哪些值可沿用。
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
