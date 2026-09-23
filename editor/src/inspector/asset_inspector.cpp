#include "inspector/asset_inspector.h"
#include "asset/artifact/shader_program_artifact.h"
#include "assets/asset_reference.h"
#include "render/material/material_programs.h"
#include "ui/dialogs.h"
#include "ui/language.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <utility>

namespace CometEditor {
    namespace {
        const std::string& property_label(const auto& property) {
            if(property.display_name.empty())
                return property.name;
            return property.display_name;
        }
        const char* texture_color_space_label(const Comet::TextureColorSpace color_space) {
            switch(color_space) {
                case Comet::TextureColorSpace::Srgb:
                    return "sRGB";
                case Comet::TextureColorSpace::Linear:
                    return "Linear";
            }
            return "Unknown";
        }
    }

    AssetInspector::AssetInspector(
        const Comet::AssetDatabase& database, const Comet::MaterialPrograms& programs)
        : m_asset_database(database), m_programs(programs) {
        const auto builtins = Comet::MaterialLayout::builtins();
        m_material_layouts.assign(builtins.begin(), builtins.end());
    }

    void AssetInspector::select(Comet::AssetHandle handle) {
        if(m_selected_asset == handle)
            return;
        m_selected_asset = handle;
        m_loaded_asset = {};
        m_loaded_revision = 0;
        m_texture_import_settings.reset();
        m_material_data.reset();
        m_asset_error.clear();
        m_template_change.reset();
        m_asset_read.reset();
        m_asset_edit.reset();
    }

    void AssetInspector::set_material_layouts(
        std::vector<std::shared_ptr<const Comet::MaterialLayout>> layouts) {
        std::erase(layouts, nullptr);
        std::ranges::sort(layouts, {}, [](const auto& layout) { return layout->get_name(); });
        m_material_layouts = std::move(layouts);
        m_template_change.reset();
        m_program_layout_source.reset();
        m_program_layout.reset();
        m_program_layout_template.clear();
        m_program_layout_error.clear();
    }

    std::shared_ptr<const Comet::MaterialLayout> AssetInspector::material_layout() const {
        if(!m_material_data)
            return nullptr;
        return layout_for(m_material_data->shader_program, m_material_data->template_name);
    }

    std::shared_ptr<const Comet::MaterialLayout> AssetInspector::layout_for(
        const Comet::AssetHandle program, const std::string& template_name) const {
        std::shared_ptr<const Comet::MaterialLayout> builtin;
        for(const auto& layout : m_material_layouts) {
            if(layout && layout->get_name() == template_name) {
                builtin = layout;
                break;
            }
        }
        if(!program)
            return builtin;
        if(const auto* active = m_programs.published(program, template_name))
            return active->layout;
        const auto source = m_programs.latest(program);
        if(!source) {
            m_program_layout_error = "Shader program is not compiled yet";
            return nullptr;
        }
        if(source == m_program_layout_source && template_name == m_program_layout_template)
            return m_program_layout;
        m_program_layout_source = source;
        m_program_layout_template = template_name;
        m_program_layout.reset();
        m_program_layout_error.clear();
        auto described = m_programs.describe(*source, template_name, builtin);
        if(!described) {
            m_program_layout_error = described.error();
            return nullptr;
        }
        m_program_layout = std::move(described).value();
        return m_program_layout;
    }

    void AssetInspector::render(std::uint64_t generation, bool allow_drop) {
        const auto handle = m_selected_asset;
        if(!handle)
            return;
        const Comet::AssetRecord* record = m_asset_database.find(handle);
        if(!record) {
            ImGui::TextColored(
                ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "Selected asset is no longer indexed");
            return;
        }

        if(m_loaded_asset != handle || !m_asset_database.is_current(handle, m_loaded_revision)) {
            load_asset(*record);
        }

        ImGui::Text(Ui::text("Path: %s"), record->path.generic_string().c_str());
        ImGui::Text(Ui::text("Type: %s"), Comet::to_string(record->type).data());
        ImGui::Separator();

        if(!m_asset_error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "%s", m_asset_error.c_str());
        }

        if(record->type == Comet::AssetType::Material) {
            if(m_material_data) {
                render_material(*record, generation, allow_drop);
                confirm_material_template();
            } else if(ImGui::Button(Ui::label("Retry Load").c_str())) {
                load_asset(*record);
            }
            return;
        }

        if(record->type == Comet::AssetType::Texture) {
            if(m_texture_import_settings) {
                render_texture(*record);
            } else if(ImGui::Button(Ui::label("Retry Load").c_str())) {
                load_asset(*record);
            }
            return;
        }

        ImGui::TextDisabled("%s", Ui::text("No inspector is available for this asset type"));
    }

    void AssetInspector::render_texture(const Comet::AssetRecord& record) {
        std::optional<Comet::TextureImportSettings> previous_settings;
        const char* color_space = texture_color_space_label(m_texture_import_settings->color_space);
        if(ImGui::BeginCombo(Ui::label("Color Space").c_str(), Ui::text(color_space))) {
            constexpr std::array color_spaces{
                Comet::TextureColorSpace::Srgb, Comet::TextureColorSpace::Linear};
            for(const Comet::TextureColorSpace candidate : color_spaces) {
                const bool selected = candidate == m_texture_import_settings->color_space;
                const char* label = texture_color_space_label(candidate);
                if(ImGui::Selectable(Ui::label(label).c_str(), selected) && !selected) {
                    if(!previous_settings) {
                        previous_settings = *m_texture_import_settings;
                    }
                    m_texture_import_settings->color_space = candidate;
                }
                if(selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        bool flip_y = m_texture_import_settings->flip_y;
        if(ImGui::Checkbox(Ui::label("Flip Y").c_str(), &flip_y)) {
            if(!previous_settings) {
                previous_settings = *m_texture_import_settings;
            }
            m_texture_import_settings->flip_y = flip_y;
        }

        if(previous_settings) {
            reimport_texture(record, *previous_settings);
        }
    }

    void AssetInspector::render_material(
        const Comet::AssetRecord& record, std::uint64_t generation, bool allow_drop) {
        std::optional<Comet::MaterialData> previous_data;
        const auto layout = material_layout();
        if(ImGui::BeginCombo(
               Ui::label("Render Template").c_str(), m_material_data->template_name.c_str())) {
            for(const auto& candidate : m_material_layouts) {
                const bool selected = candidate->get_name() == m_material_data->template_name;
                if(ImGui::Selectable(candidate->get_name().c_str(), selected) && !selected)
                    m_template_change =
                        change_material_template(*m_material_data, layout.get(), *candidate);
                if(selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const auto remember_previous = [&] {
            if(!previous_data)
                previous_data = *m_material_data;
        };
        ImGui::BeginDisabled(m_template_change.has_value());
        const auto select_program = [&](const Comet::AssetHandle program) {
            if(program == m_material_data->shader_program)
                return;
            if(program && !m_programs.latest(program)) {
                m_asset_error = "Shader program is not compiled yet";
                return;
            }
            const auto next_layout = layout_for(program, m_material_data->template_name);
            if(!next_layout) {
                m_asset_error = "Shader program layout is unavailable";
                if(!m_program_layout_error.empty())
                    m_asset_error = m_program_layout_error;
                return;
            }
            m_template_change =
                change_material_template(*m_material_data, layout.get(), *next_layout);
            m_template_change->data.shader_program = program;
            m_asset_error.clear();
        };
        auto shader_program = m_material_data->shader_program;
        if(edit_asset_reference(Ui::label("Shader Program").c_str(), shader_program,
               m_asset_database, Comet::AssetType::ShaderProgram))
            select_program(shader_program);
        if(allow_drop) {
            if(const auto asset = accept_asset_drop(
                   m_asset_database, Comet::AssetType::ShaderProgram, generation);
                asset)
                select_program(asset->handle);
        }
        if(!layout) {
            ImGui::TextDisabled("%s", Ui::text("No registered layout for this material"));
            if(!m_program_layout_error.empty())
                ImGui::TextWrapped("%s", m_program_layout_error.c_str());
            ImGui::EndDisabled();
            return;
        }
        if(!layout->get_textures().empty())
            ImGui::SeparatorText(Ui::text("Textures"));

        for(const auto& property : layout->get_textures()) {
            const auto& property_name = property.name;
            const auto found = m_material_data->texture_properties.find(property_name);
            Comet::AssetHandle texture_handle;
            if(found != m_material_data->texture_properties.end())
                texture_handle = found->second;
            ImGui::PushID(property_name.c_str());
            const auto& label = property_label(property);
            const auto assign = [&](Comet::AssetHandle value) {
                if(value == texture_handle)
                    return;
                remember_previous();
                m_material_data->scalar_properties.erase(property_name);
                m_material_data->vector_properties.erase(property_name);
                if(value)
                    m_material_data->texture_properties[property_name] = value;
                else
                    m_material_data->texture_properties.erase(property_name);
                texture_handle = value;
            };
            auto selected = texture_handle;
            if(edit_asset_reference(label.c_str(), selected, m_asset_database,
                   Comet::AssetType::Texture, property.optional)) {
                assign(selected);
            }
            if(allow_drop) {
                if(const auto asset =
                        accept_asset_drop(m_asset_database, Comet::AssetType::Texture, generation);
                    asset && asset->handle != texture_handle)
                    assign(asset->handle);
            }
            ImGui::PopID();
        }

        if(!layout->get_scalars().empty() || !layout->get_vectors().empty())
            ImGui::SeparatorText(Ui::text("Parameters"));
        for(const auto& property : layout->get_scalars()) {
            const auto found = m_material_data->scalar_properties.find(property.name);
            float value = property.default_value;
            if(found != m_material_data->scalar_properties.end())
                value = found->second;
            const float before = value;
            const auto& label = property_label(property);
            ImGui::PushID(property.name.c_str());
            if(ImGui::DragFloat(Ui::label(label.c_str()).c_str(), &value, property.step,
                   property.min_value, property.max_value, "%.3f", ImGuiSliderFlags_AlwaysClamp)
                && value != before) {
                remember_previous();
                m_material_data->texture_properties.erase(property.name);
                m_material_data->vector_properties.erase(property.name);
                m_material_data->scalar_properties[property.name] = value;
            }
            ImGui::PopID();
        }
        for(const auto& property : layout->get_vectors()) {
            const auto found = m_material_data->vector_properties.find(property.name);
            auto value = property.default_value;
            if(found != m_material_data->vector_properties.end())
                value = found->second;
            const auto before = value;
            const auto& label = property_label(property);
            ImGui::PushID(property.name.c_str());
            bool changed = false;
            if(property.semantic == Comet::MaterialLayout::VectorProperty::Semantic::Color) {
                changed = ImGui::ColorEdit4(
                    Ui::label(label.c_str()).c_str(), &value.x, ImGuiColorEditFlags_Float);
            } else {
                changed = ImGui::DragFloat4(Ui::label(label.c_str()).c_str(), &value.x, 0.01f);
            }
            if(changed && value != before) {
                remember_previous();
                m_material_data->texture_properties.erase(property.name);
                m_material_data->scalar_properties.erase(property.name);
                m_material_data->vector_properties[property.name] = value;
            }
            ImGui::PopID();
        }

        const std::string validation_error = validate_material();
        if(!validation_error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "%s", validation_error.c_str());
        }

        if(previous_data && validation_error.empty()) {
            update_material(record, *previous_data);
        }
        ImGui::EndDisabled();
    }

    void AssetInspector::confirm_material_template() {
        if(m_selected_asset != m_loaded_asset
            || !m_asset_database.is_current(m_loaded_asset, m_loaded_revision))
            m_template_change.reset();
        std::string template_name;
        std::span<const std::string> discarded;
        if(m_template_change) {
            template_name = m_template_change->data.template_name;
            discarded = m_template_change->discarded_properties;
        }
        const auto decision =
            draw_material_template_dialog(m_template_change.has_value(), template_name, discarded);
        if(!decision || !m_template_change)
            return;
        if(*decision) {
            const auto previous = *m_material_data;
            *m_material_data = m_template_change->data;
            const auto error = validate_material();
            if(error.empty())
                update_material(*m_asset_database.find(m_loaded_asset), previous);
            else {
                *m_material_data = previous;
                m_asset_error = error;
            }
        }
        m_template_change.reset();
    }

    void AssetInspector::load_asset(const Comet::AssetRecord& record) {
        m_loaded_asset = record.handle;
        m_loaded_revision = m_asset_database.get_revision(record.handle);
        m_texture_import_settings.reset();
        m_material_data.reset();
        m_template_change.reset();
        m_asset_error.clear();

        if(record.type == Comet::AssetType::Texture) {
            const auto* settings =
                std::get_if<Comet::TextureImportSettings>(&record.import_settings);
            if(settings) {
                m_texture_import_settings = *settings;
            } else {
                m_asset_error = "Texture has incompatible import settings";
            }
            return;
        }

        if(record.type != Comet::AssetType::Material) {
            return;
        }

        m_asset_read = AssetRead{record.handle, m_loaded_revision};
    }

    std::optional<AssetRead> AssetInspector::take_asset_read() {
        return std::exchange(m_asset_read, std::nullopt);
    }

    void AssetInspector::complete_asset_read(
        const AssetRead& request, Comet::Result<Comet::MaterialData> result) {
        if(request.handle != m_loaded_asset || request.revision != m_loaded_revision
            || m_selected_asset != request.handle
            || !m_asset_database.is_current(request.handle, request.revision))
            return;
        if(result)
            m_material_data = std::move(result).value();
        else
            m_asset_error = result.error();
    }

    void AssetInspector::reimport_texture(
        const Comet::AssetRecord& record, const Comet::TextureImportSettings& previous_settings) {
        if(!m_texture_import_settings) {
            return;
        }
        m_asset_edit = AssetEdit{record.handle, m_loaded_revision,
            TextureEdit{previous_settings, *m_texture_import_settings}};
    }

    void AssetInspector::update_material(
        const Comet::AssetRecord& record, const Comet::MaterialData& previous_data) {
        if(!m_material_data) {
            return;
        }

        m_asset_edit = AssetEdit{
            record.handle, m_loaded_revision, MaterialEdit{previous_data, *m_material_data}};
    }

    std::optional<AssetEdit> AssetInspector::take_asset_edit() {
        return std::exchange(m_asset_edit, std::nullopt);
    }

    void AssetInspector::complete_asset_edit(
        const AssetEdit& edit, const bool succeeded, std::string error) {
        if(m_loaded_asset != edit.handle || m_loaded_revision != edit.revision)
            return;
        m_asset_error = std::move(error);
        if(succeeded)
            return;
        if(const auto* material = std::get_if<MaterialEdit>(&edit.value))
            m_material_data = material->before;
        else
            m_texture_import_settings = std::get<TextureEdit>(edit.value).before;
    }

    std::string AssetInspector::validate_material() const {
        if(!m_material_data)
            return "Material data is not loaded";
        const auto layout = material_layout();
        if(!layout)
            return "Material layout is not registered";
        const auto result = validate_material_data(*m_material_data, *layout, m_asset_database);
        if(!result)
            return result.error();
        return {};
    }
}
