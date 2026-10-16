#include "inspector/asset_inspector.h"
#include "inspector/animation_preview.h"
#include <imgui.h>
#include "ui/widgets.h"
#include <set>

namespace CometEditor {
    void AssetInspector::complete_asset_read(
        const Comet::AssetTarget& request, Comet::Result<Comet::AnimationLibrary> result) {
        if(request != m_loaded_target || !m_asset_database.is_current(request))
            return;
        if(result) {
            m_library_saved = result.value();
            m_library_data = std::move(result).value();
        } else
            m_asset_error = result.error();
    }
    bool AssetInspector::animation_dirty() const {
        return (m_controller_data && m_controller_saved
                   && *m_controller_data != *m_controller_saved)
               || (m_library_data && m_library_saved && *m_library_data != *m_library_saved);
    }
    bool AssetInspector::finish_animation_edit() {
        if(!animation_dirty())
            return true;
        auto valid = m_controller_data ? m_controller_data->validate() : m_library_data->validate();
        if(!valid) {
            m_asset_error = "动画资产尚未保存：" + valid.error();
            m_selection_request = m_selected_asset;
            return false;
        }
        if(m_controller_data)
            m_asset_edit = AssetEdit{
                m_loaded_target, AnimationControllerEdit{*m_controller_saved, *m_controller_data}};
        else
            m_asset_edit =
                AssetEdit{m_loaded_target, AnimationLibraryEdit{*m_library_saved, *m_library_data}};
        return true;
    }

    bool AssetInspector::undo_controller(bool redo) {
        if(!m_controller_data)
            return false;
        return redo ? m_controller_editor.redo(*m_controller_data)
                    : m_controller_editor.undo(*m_controller_data);
    }

    std::optional<Comet::AssetTarget> AssetInspector::take_library_read() {
        const auto handle = m_controller_data ? m_controller_data->library : Comet::AssetHandle{};
        const Comet::AssetTarget target{handle, m_asset_database.get_revision(handle)};
        if(target == m_library_target)
            return std::nullopt;
        m_library_target = target;
        if(!handle) {
            m_controller_editor.set_library(nullptr);
            return std::nullopt;
        }
        return target;
    }
    void AssetInspector::render_controller_window(AnimationPreview* preview) {
        if(!m_controller_data || !m_asset_database.is_current(m_loaded_target))
            return;
        const auto action = m_controller_editor.render_window(*m_controller_data,
            *m_controller_data != *m_controller_saved, preview,
            m_asset_database.find(m_loaded_target.handle)->path.generic_string());
        if(action == AnimationControllerEditor::Action::Save)
            static_cast<void>(finish_animation_edit());
        else if(action == AnimationControllerEditor::Action::Discard) {
            m_controller_data = *m_controller_saved;
            m_controller_editor.reset(*m_controller_data);
        }
    }

    void AssetInspector::render_animation_controller() {
        auto& data = *m_controller_data;
        std::vector<std::pair<Comet::AssetHandle, std::string>> libraries;
        for(const auto& record : m_asset_database.get_assets())
            if(record.type == Comet::AssetType::AnimationLibrary)
                libraries.emplace_back(record.handle, record.path.generic_string());
        m_controller_editor.set_library_choices(std::move(libraries));
        if(ImGui::Button("打开动画控制器###Open Animation Controller"))
            m_controller_editor.open();
        m_controller_editor.render_details(data);
        const bool dirty = data != *m_controller_saved;
        ImGui::Separator();
        ImGui::TextDisabled("%s", dirty ? "有未保存修改" : "已保存");
        ImGui::BeginDisabled(!data.validate() || !dirty);
        if(ImGui::Button("保存控制器###Save Controller"))
            static_cast<void>(finish_animation_edit());
        ImGui::EndDisabled();
        render_unsaved_animation();
    }

    void AssetInspector::render_unsaved_animation() {
        if(m_next_asset && animation_dirty())
            ImGui::OpenPopup("未保存的动画资产");
        if(!ImGui::BeginPopupModal("未保存的动画资产", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextUnformatted("切换前保存当前动画资产？");
        const bool valid = m_controller_data ? bool(m_controller_data->validate())
                                             : bool(m_library_data->validate());
        ImGui::BeginDisabled(!valid);
        if(ImGui::Button("保存并切换") && finish_animation_edit())
            ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("放弃并切换")) {
            const auto next = *m_next_asset;
            if(m_controller_data)
                m_controller_data = *m_controller_saved;
            else
                m_library_data = *m_library_saved;
            select(next);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("继续编辑")) {
            m_selection_request = m_selected_asset;
            m_next_asset.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void AssetInspector::render_animation_library() {
        auto& data = *m_library_data;
        ImGui::TextWrapped(
            "在动画控制器中引用此库；不同骨架可按来源启用重定向。同名骨骼自动匹配，其余在这里映射。片段别名与来源可通过打开源码编辑。");
        ImGui::Separator();
        ImGui::TextDisabled("%s", animation_dirty() ? "有未保存修改" : "已保存");
        ImGui::BeginDisabled(!data.validate() || !animation_dirty());
        if(ImGui::Button("保存动画库###Save Animation Library"))
            static_cast<void>(finish_animation_edit());
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("放弃修改###Discard Animation Library"))
            data = *m_library_saved;
        if(ImGui::BeginTable("Animation library", 3,
               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("别名");
            ImGui::TableSetupColumn("源网格");
            ImGui::TableSetupColumn("片段");
            ImGui::TableHeadersRow();
            for(const auto& entry : data.entries) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.name.c_str());
                ImGui::TableNextColumn();
                const auto* source = m_asset_database.find(entry.mesh);
                const auto label =
                    source ? source->path.generic_string() : std::to_string(entry.mesh.value());
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.clip.c_str());
            }
            ImGui::EndTable();
        }
        std::set<Comet::AssetHandle> sources;
        for(const auto& entry : data.entries)
            sources.insert(entry.mesh);
        for(const auto source : sources) {
            ImGui::PushID(std::to_string(source.value()).c_str());
            const auto* record = m_asset_database.find(source);
            const auto label =
                record ? record->path.generic_string() : std::to_string(source.value());
            ImGui::SeparatorText(label.c_str());
            bool enabled = data.retargets.contains(source);
            if(ImGui::Checkbox("启用重定向", &enabled)) {
                if(enabled)
                    data.retargets.emplace(source, Comet::AnimationRetarget{});
                else
                    data.retargets.erase(source);
            }
            if(enabled) {
                auto& settings = data.retargets.at(source);
                bool automatic = settings.translation_scale == 0;
                if(ImGui::Checkbox("自动位移比例", &automatic))
                    settings.translation_scale = automatic ? 0 : 1;
                if(!automatic)
                    ImGui::DragFloat("位移比例", &settings.translation_scale, .01f, .001f, 100);
                std::optional<std::string> removed;
                std::optional<std::pair<std::string, std::string>> renamed;
                for(auto& [from, to] : settings.nodes) {
                    ImGui::PushID(from.c_str());
                    auto name = from;
                    if(Ui::input_text(
                           "源骨骼（回车确认）", name, ImGuiInputTextFlags_EnterReturnsTrue)
                        && name != from && !name.empty() && !settings.nodes.contains(name))
                        renamed = {from, std::move(name)};
                    Ui::input_text("目标骨骼", to);
                    if(ImGui::SmallButton("删除映射"))
                        removed = from;
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if(removed)
                    settings.nodes.erase(*removed);
                if(renamed) {
                    auto node = settings.nodes.extract(renamed->first);
                    if(!node.empty()) {
                        node.key() = renamed->second;
                        settings.nodes.insert(std::move(node));
                        for(auto& name : settings.translations)
                            if(name == renamed->first)
                                name = renamed->second;
                    }
                }
                if(ImGui::SmallButton("添加骨骼映射")) {
                    for(unsigned i = 1;; ++i) {
                        const auto name = "source_bone" + std::to_string(i);
                        if(settings.nodes.emplace(name, "target_bone").second)
                            break;
                    }
                }
                std::optional<std::size_t> remove_translation;
                for(std::size_t i = 0; i < settings.translations.size(); ++i) {
                    ImGui::PushID(int(i));
                    Ui::input_text("允许位移的源骨骼", settings.translations[i]);
                    if(ImGui::SmallButton("移除位移骨骼"))
                        remove_translation = i;
                    ImGui::PopID();
                }
                if(remove_translation)
                    settings.translations.erase(
                        settings.translations.begin() + *remove_translation);
                if(ImGui::SmallButton("添加位移骨骼"))
                    settings.translations.emplace_back();
                ImGui::TextWrapped(
                    "目标骨长保持不变；只转移所选骨骼的平移。自动比例使用对应骨段长度比的中位数；在控制器中选择目标模型试播验证映射。");
            }
            ImGui::PopID();
        }
        const auto valid = data.validate();
        if(!valid)
            ImGui::TextColored({1, .45f, .35f, 1}, "%s", valid.error().c_str());
        render_unsaved_animation();
    }
}
