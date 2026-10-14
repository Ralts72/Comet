#include "inspector/asset_inspector.h"
#include "asset/artifact/shader_program_artifact.h"
#include "assets/asset_reference.h"
#include "render/material/material_programs.h"
#include "ui/dialogs.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <string_view>
#include <span>
#include <utility>

namespace CometEditor {
    namespace {
        std::string property_label(const auto& property, const bool builtin) {
            const auto& original =
                property.display_name.empty() ? property.name : property.display_name;
            if(builtin) {
                static constexpr std::pair<std::string_view, std::string_view> captions[]{
                    {"intensity", "强度"},
                    {"color", "颜色"},
                    {"base_color_texture", "基础色贴图"},
                    {"normal_texture", "法线贴图"},
                    {"metallic_roughness_texture", "金属粗糙度贴图（G 粗糙／B 金属）"},
                    {"occlusion_texture", "环境遮蔽贴图（R）"},
                    {"emissive_texture", "自发光贴图"},
                    {"normal_scale", "法线强度"},
                    {"occlusion_strength", "环境遮蔽强度"},
                    {"emissive_color", "自发光颜色"},
                    {"emissive_strength", "自发光强度"},
                    {"metallic", "金属度"},
                    {"roughness", "粗糙度"},
                    {"base_color", "基础色"},
                };
                for(const auto& [name, caption] : captions)
                    if(name == property.name)
                        return std::string(caption) + "###" + original;
            }
            return original;
        }
        const char* texture_color_space_label(const Comet::TextureColorSpace color_space) {
            switch(color_space) {
                case Comet::TextureColorSpace::Srgb:
                    return "sRGB###sRGB";
                case Comet::TextureColorSpace::Linear:
                    return "线性###Linear";
            }
            return "Unknown";
        }
    }

    AssetInspector::AssetInspector(
        const Comet::AssetDatabase& database, const Comet::MaterialProgramValidator& programs)
        : m_asset_database(database), m_material_parameters(programs) {
        const auto builtins = Comet::MaterialLayout::builtins();
        m_material_layouts.assign(builtins.begin(), builtins.end());
    }

    void AssetInspector::select(Comet::AssetHandle handle) {
        if(m_selected_asset == handle) {
            m_next_asset.reset();
            return;
        }
        if(m_controller_data && m_controller_saved && *m_controller_data != *m_controller_saved) {
            m_next_asset = handle;
            return;
        }
        if(m_material_before) {
            if(m_next_asset && *m_next_asset == handle)
                return;
            m_next_asset = handle;
            finish_material_edit();
            return;
        }
        m_selected_asset = handle;
        m_loaded_target = {};
        m_texture_import_settings.reset();
        m_material_data.reset();
        m_controller_data.reset();
        m_controller_saved.reset();
        m_controller_editor.close();
        m_asset_error.clear();
        m_template_change.reset();
        m_asset_read.reset();
        m_asset_edit.reset();
        m_next_asset.reset();
    }

    void AssetInspector::open_controller(Comet::AssetHandle handle) {
        select(handle);
        if(m_selected_asset != handle)
            return;
        const auto* record = m_asset_database.find(handle);
        if(!record || record->type != Comet::AssetType::AnimationController)
            return;
        if(m_loaded_target.handle != handle || !m_asset_database.is_current(m_loaded_target))
            load_asset(*record);
        m_controller_editor.open();
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
        if(const auto* active = m_material_parameters.published(program, template_name))
            return active->layout;
        const auto source = m_material_parameters.latest(program);
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
        auto described = Comet::MaterialPrograms::describe(*source, template_name, builtin);
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

        if(record->source_asset) {
            ImGui::Text("源模型：%s", record->path.generic_string().c_str());
            ImGui::Text("子资源：%s", record->source_key.c_str());
            ImGui::TextWrapped(
                "由模型导入生成。请修改源模型后重新导入；实例可在网格渲染组件中替换材质。");
            return;
        }

        if(m_loaded_target.handle != handle
            || !m_asset_database.is_current(handle, m_loaded_target.revision)) {
            load_asset(*record);
        }

        ImGui::Text("路径：%s", record->path.generic_string().c_str());
        ImGui::Text("类型：%s", Comet::to_string(record->type).data());
        ImGui::Separator();

        if(!m_asset_error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "%s", m_asset_error.c_str());
        }

        if(record->type == Comet::AssetType::AnimationController) {
            if(m_controller_data)
                render_animation_controller();
            else if(ImGui::Button("重试加载###Retry Load"))
                load_asset(*record);
            return;
        }
        if(record->type == Comet::AssetType::Material) {
            if(m_material_data) {
                render_material(*record, generation, allow_drop);
                confirm_material_template();
            } else if(ImGui::Button("重试加载###Retry Load")) {
                load_asset(*record);
            }
            return;
        }

        if(record->type == Comet::AssetType::Texture) {
            if(m_texture_import_settings) {
                render_texture(*record);
            } else if(ImGui::Button("重试加载###Retry Load")) {
                load_asset(*record);
            }
            return;
        }

        ImGui::TextDisabled("%s", "此资产类型暂无属性编辑器");
    }

    void AssetInspector::render_texture(const Comet::AssetRecord& record) {
        std::optional<Comet::TextureImportSettings> previous_settings;
        bool normal_map = m_texture_import_settings->normal_map;
        if(ImGui::Checkbox("法线贴图###Normal Map", &normal_map)) {
            previous_settings = *m_texture_import_settings;
            m_texture_import_settings->normal_map = normal_map;
        }
        ImGui::BeginDisabled(normal_map);
        const char* color_space = texture_color_space_label(m_texture_import_settings->color_space);
        if(normal_map)
            color_space = "线性（法线）";
        if(ImGui::BeginCombo("色彩空间###Color Space", color_space)) {
            constexpr std::array color_spaces{
                Comet::TextureColorSpace::Srgb, Comet::TextureColorSpace::Linear};
            for(const Comet::TextureColorSpace candidate : color_spaces) {
                const bool selected = candidate == m_texture_import_settings->color_space;
                const char* label = texture_color_space_label(candidate);
                if(ImGui::Selectable(label, selected) && !selected) {
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
        ImGui::EndDisabled();

        bool flip_y = m_texture_import_settings->flip_y;
        if(ImGui::Checkbox("翻转 Y###Flip Y", &flip_y)) {
            if(!previous_settings) {
                previous_settings = *m_texture_import_settings;
            }
            m_texture_import_settings->flip_y = flip_y;
        }

        bool mipmaps = m_texture_import_settings->generate_mipmaps;
        if(ImGui::Checkbox("生成 Mipmap###Generate Mipmaps", &mipmaps)) {
            if(!previous_settings)
                previous_settings = *m_texture_import_settings;
            m_texture_import_settings->generate_mipmaps = mipmaps;
        }

        if(previous_settings) {
            reimport_texture(record, *previous_settings);
        }
    }

    void AssetInspector::render_material(
        const Comet::AssetRecord& record, std::uint64_t generation, bool allow_drop) {
        std::optional<Comet::MaterialData> previous_data;
        bool parameter_changed = false;
        bool parameter_active = false;
        const auto layout = material_layout();
        const bool builtin = layout && !m_material_data->shader_program
                             && Comet::MaterialLayout::find_builtin(layout->get_name());
        ImGui::BeginDisabled(m_material_before.has_value());
        if(ImGui::BeginCombo(
               "渲染模板###Render Template", m_material_data->template_name.c_str())) {
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
        ImGui::EndDisabled();
        const auto remember_previous = [&] {
            if(!previous_data)
                previous_data = *m_material_data;
        };
        ImGui::BeginDisabled(m_template_change.has_value());
        ImGui::BeginDisabled(m_material_before.has_value());
        const auto select_program = [&](const Comet::AssetHandle program) {
            if(program == m_material_data->shader_program)
                return;
            if(program && !m_material_parameters.latest(program)) {
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
        if(edit_asset_reference("着色器程序###Shader Program", shader_program, m_asset_database,
               Comet::AssetType::ShaderProgram))
            select_program(shader_program);
        if(allow_drop) {
            if(const auto asset = accept_asset_drop(
                   m_asset_database, Comet::AssetType::ShaderProgram, generation);
                asset)
                select_program(asset->target.handle);
        }
        if(!layout) {
            ImGui::TextDisabled("%s", "材质模板尚未注册");
            if(!m_program_layout_error.empty())
                ImGui::TextWrapped("%s", m_program_layout_error.c_str());
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            return;
        }
        ImGui::SeparatorText("表面");
        const char* modes[]{"不透明", "透明裁切", "透明混合"};
        auto surface = m_material_data->surface;
        int mode = static_cast<int>(surface.alpha_mode);
        if(ImGui::Combo("表面模式###Alpha Mode", &mode, modes, 3)) {
            remember_previous();
            m_material_data->surface.alpha_mode = static_cast<Comet::AlphaMode>(mode);
        }
        if(ImGui::Checkbox("双面绘制###Double Sided", &surface.double_sided)) {
            remember_previous();
            m_material_data->surface.double_sided = surface.double_sided;
        }
        if(!layout->get_textures().empty())
            ImGui::SeparatorText("纹理");

        for(const auto& property : layout->get_textures()) {
            const auto& property_name = property.name;
            const auto found = m_material_data->texture_properties.find(property_name);
            Comet::AssetHandle texture_handle;
            if(found != m_material_data->texture_properties.end())
                texture_handle = found->second;
            ImGui::PushID(property_name.c_str());
            const auto& label = property_label(property, builtin);
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
                    asset && asset->target.handle != texture_handle)
                    assign(asset->target.handle);
            }
            ImGui::PopID();
        }

        ImGui::EndDisabled();
        if(m_material_data->surface.alpha_mode == Comet::AlphaMode::Mask) {
            float cutoff = m_material_data->surface.alpha_cutoff;
            if(ImGui::SliderFloat("裁切阈值###Alpha Cutoff", &cutoff, 0, 1, "%.3f")) {
                remember_previous();
                m_material_data->surface.alpha_cutoff = cutoff;
                parameter_changed = true;
            }
            parameter_active |= ImGui::IsItemActive();
        }
        if(!layout->get_scalars().empty() || !layout->get_vectors().empty())
            ImGui::SeparatorText("参数");
        for(const auto& property : layout->get_scalars()) {
            const auto found = m_material_data->scalar_properties.find(property.name);
            float value = property.default_value;
            if(found != m_material_data->scalar_properties.end())
                value = found->second;
            const float before = value;
            const auto& label = property_label(property, builtin);
            ImGui::PushID(property.name.c_str());
            if(ImGui::DragFloat(label.c_str(), &value, property.step, property.min_value,
                   property.max_value, "%.3f", ImGuiSliderFlags_AlwaysClamp)
                && value != before) {
                remember_previous();
                m_material_data->texture_properties.erase(property.name);
                m_material_data->vector_properties.erase(property.name);
                m_material_data->scalar_properties[property.name] = value;
                parameter_changed = true;
            }
            parameter_active |= ImGui::IsItemActive();
            ImGui::PopID();
        }
        for(const auto& property : layout->get_vectors()) {
            const auto found = m_material_data->vector_properties.find(property.name);
            auto value = property.default_value;
            if(found != m_material_data->vector_properties.end())
                value = found->second;
            const auto before = value;
            const auto& label = property_label(property, builtin);
            ImGui::PushID(property.name.c_str());
            bool changed = false;
            if(property.semantic == Comet::MaterialLayout::VectorProperty::Semantic::Color) {
                changed = ImGui::ColorEdit4(label.c_str(), &value.x, ImGuiColorEditFlags_Float);
            } else {
                changed = ImGui::DragFloat4(label.c_str(), &value.x, 0.01f);
            }
            if(changed && value != before) {
                remember_previous();
                m_material_data->texture_properties.erase(property.name);
                m_material_data->scalar_properties.erase(property.name);
                m_material_data->vector_properties[property.name] = value;
                parameter_changed = true;
            }
            parameter_active |= ImGui::IsItemActive();
            ImGui::PopID();
        }

        const std::string validation_error = validate_material();
        if(!validation_error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "%s", validation_error.c_str());
        }

        if(parameter_active)
            m_material_active_item = ImGui::GetActiveID();
        if(m_material_before && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            finish_material_edit(true);
        } else if(previous_data && validation_error.empty()) {
            if(parameter_changed && !m_material_before)
                m_material_before = *previous_data;
            if(m_material_before) {
                auto action = AssetEdit::Action::Commit;
                if(parameter_active)
                    action = AssetEdit::Action::Preview;
                update_material(record, *m_material_before, action);
            } else {
                update_material(record, *previous_data);
            }
        } else if(m_material_before && !parameter_active && m_asset_error.empty()) {
            finish_material_edit();
        }
        ImGui::EndDisabled();
        if(m_material_before && !m_asset_error.empty()) {
            if(ImGui::Button("重试保存###Retry Save"))
                finish_material_edit();
            ImGui::SameLine();
            if(ImGui::Button("取消修改###Cancel Changes"))
                finish_material_edit(true);
        }
    }

    void AssetInspector::confirm_material_template() {
        if(m_selected_asset != m_loaded_target.handle
            || !m_asset_database.is_current(m_loaded_target))
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
                update_material(*m_asset_database.find(m_loaded_target.handle), previous);
            else {
                *m_material_data = previous;
                m_asset_error = error;
            }
        }
        m_template_change.reset();
    }

    void AssetInspector::load_asset(const Comet::AssetRecord& record) {
        m_loaded_target = {record.handle, m_asset_database.get_revision(record.handle)};
        m_texture_import_settings.reset();
        m_material_data.reset();
        m_controller_data.reset();
        m_controller_saved.reset();
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

        if(record.type != Comet::AssetType::Material
            && record.type != Comet::AssetType::AnimationController) {
            return;
        }

        m_asset_read = Comet::AssetTarget{record.handle, m_loaded_target.revision};
    }

    std::optional<Comet::AssetTarget> AssetInspector::take_asset_read() {
        return std::exchange(m_asset_read, std::nullopt);
    }

    void AssetInspector::complete_asset_read(
        const Comet::AssetTarget& request, Comet::Result<Comet::MaterialData> result) {
        if(request != m_loaded_target || m_selected_asset != request.handle
            || !m_asset_database.is_current(request))
            return;
        if(result)
            m_material_data = std::move(result).value();
        else
            m_asset_error = result.error();
    }

    void AssetInspector::complete_asset_read(
        const Comet::AssetTarget& request, Comet::Result<Comet::AnimationController> result) {
        if(request != m_loaded_target || m_selected_asset != request.handle
            || !m_asset_database.is_current(request))
            return;
        if(result) {
            m_controller_data = std::move(result).value();
            m_controller_saved = m_controller_data;
            m_controller_editor.reset(*m_controller_data);
        } else
            m_asset_error = result.error();
    }
    std::optional<Comet::AssetHandle> AssetInspector::take_selection_request() {
        return std::exchange(m_selection_request, std::nullopt);
    }

    void AssetInspector::reimport_texture(
        const Comet::AssetRecord& record, const Comet::TextureImportSettings& previous_settings) {
        if(!m_texture_import_settings) {
            return;
        }
        m_asset_edit = AssetEdit{{record.handle, m_loaded_target.revision},
            TextureEdit{previous_settings, *m_texture_import_settings}};
    }

    void AssetInspector::update_material(const Comet::AssetRecord& record,
        const Comet::MaterialData& previous_data, const AssetEdit::Action action) {
        if(!m_material_data) {
            return;
        }

        m_asset_edit = AssetEdit{{record.handle, m_loaded_target.revision},
            MaterialEdit{previous_data, *m_material_data}, action};
    }

    void AssetInspector::finish_material_edit(const bool cancel) {
        if(!m_material_before || !m_material_data)
            return;
        if(m_material_active_item && ImGui::GetCurrentContext()
            && ImGui::GetActiveID() == m_material_active_item)
            ImGui::ClearActiveID();
        m_material_active_item = 0;
        auto action = AssetEdit::Action::Commit;
        if(cancel) {
            action = AssetEdit::Action::Cancel;
            m_material_data = *m_material_before;
        }
        m_asset_edit =
            AssetEdit{m_loaded_target, MaterialEdit{*m_material_before, *m_material_data}, action};
    }

    std::optional<AssetEdit> AssetInspector::take_asset_edit() {
        return std::exchange(m_asset_edit, std::nullopt);
    }

    void AssetInspector::complete_asset_edit(
        const AssetEdit& edit, const bool succeeded, std::string error) {
        if(m_loaded_target != edit.target)
            return;
        m_asset_error = std::move(error);
        if(const auto* controller = std::get_if<AnimationControllerEdit>(&edit.value)) {
            if(succeeded) {
                m_controller_saved = controller->after;
                m_loaded_target.revision = m_asset_database.get_revision(edit.target.handle);
                if(m_next_asset) {
                    const auto next = *m_next_asset;
                    m_next_asset.reset();
                    select(next);
                }
            }
            return;
        }
        if(edit.action == AssetEdit::Action::Preview)
            return;
        if(edit.action == AssetEdit::Action::Commit || edit.action == AssetEdit::Action::Cancel) {
            if(!succeeded && edit.action == AssetEdit::Action::Commit)
                return;
            m_material_before.reset();
            m_loaded_target.revision = m_asset_database.get_revision(edit.target.handle);
            if(m_next_asset) {
                const auto next = *m_next_asset;
                m_next_asset.reset();
                select(next);
            }
            return;
        }
        if(succeeded) {
            m_loaded_target.revision = m_asset_database.get_revision(edit.target.handle);
            return;
        }
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
