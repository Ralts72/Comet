#include "inspector/asset_inspector.h"
#include "ui/widgets.h"
#include <imgui.h>
#include <algorithm>
#include <array>

namespace CometEditor {
    namespace {
        void scalar(const char* label, Comet::ParameterValue& value) {
            if(auto* boolean = std::get_if<bool>(&value))
                ImGui::Checkbox(label, boolean);
            else if(auto* number = std::get_if<float>(&value))
                ImGui::DragFloat(label, number, 0.05f);
        }
        void state_choice(const char* label, std::string& selected,
            const Comet::AnimationController& controller, bool any = false) {
            const char* caption = selected.empty() ? "任意状态" : selected.c_str();
            if(!ImGui::BeginCombo(label, caption))
                return;
            if(any && ImGui::Selectable("任意状态", selected.empty()))
                selected.clear();
            for(const auto& state : controller.states)
                if(ImGui::Selectable(state.name.c_str(), state.name == selected))
                    selected = state.name;
            ImGui::EndCombo();
        }
        template<typename T, typename Draw>
        void rows(std::vector<T>& values, const char* title, Draw draw) {
            ImGui::PushID(title);
            std::optional<size_t> removed;
            for(size_t index = 0; index < values.size(); ++index) {
                ImGui::PushID(int(index));
                if(ImGui::TreeNodeEx(
                       "row", ImGuiTreeNodeFlags_DefaultOpen, "%s %zu", title, index + 1)) {
                    draw(values[index]);
                    if(ImGui::SmallButton("删除"))
                        removed = index;
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if(removed)
                values.erase(values.begin() + *removed);
            if(ImGui::Button((std::string("添加") + title).c_str()))
                values.emplace_back();
            ImGui::PopID();
        }
    }
    bool AssetInspector::finish_controller_edit() {
        if(!m_controller_data || !m_controller_saved || *m_controller_data == *m_controller_saved)
            return true;
        if(auto valid = m_controller_data->validate(); !valid) {
            m_asset_error = "控制器尚未保存：" + valid.error();
            m_selection_request = m_selected_asset;
            return false;
        }
        m_asset_edit = AssetEdit{
            m_loaded_target, AnimationControllerEdit{*m_controller_saved, *m_controller_data}};
        return true;
    }

    void AssetInspector::render_animation_controller() {
        auto& data = *m_controller_data;
        ImGui::TextWrapped("参数驱动状态切换；过渡按列表顺序匹配，每帧最多切换一次。");
        state_choice("初始状态", data.initial, data);
        if(ImGui::CollapsingHeader("参数", ImGuiTreeNodeFlags_DefaultOpen)) {
            std::optional<std::pair<std::string, std::string>> renamed;
            std::optional<std::string> removed;
            for(auto& [name, value] : data.parameters) {
                ImGui::PushID(name.c_str());
                auto edited = name;
                if(Ui::input_text("名称（回车确认）", edited, ImGuiInputTextFlags_EnterReturnsTrue))
                    renamed = {name, std::move(edited)};
                bool boolean = std::holds_alternative<bool>(value);
                if(ImGui::Checkbox("布尔参数", &boolean))
                    value = boolean ? Comet::ParameterValue(false) : Comet::ParameterValue(0.f);
                scalar("默认值", value);
                if(ImGui::SmallButton("删除参数"))
                    removed = name;
                ImGui::PopID();
            }
            if(removed)
                data.parameters.erase(*removed);
            if(renamed && !data.parameters.contains(renamed->second)) {
                auto node = data.parameters.extract(renamed->first);
                if(!node.empty()) {
                    node.key() = renamed->second;
                    data.parameters.insert(std::move(node));
                    for(auto& state : data.states)
                        if(state.parameter == renamed->first)
                            state.parameter = renamed->second;
                    for(auto& transition : data.transitions)
                        for(auto& condition : transition.conditions)
                            if(condition.parameter == renamed->first)
                                condition.parameter = renamed->second;
                }
            }
            if(ImGui::Button("添加参数")) {
                for(unsigned index = 1;; ++index)
                    if(data.parameters.emplace("parameter" + std::to_string(index), 0.f).second)
                        break;
            }
        }
        if(ImGui::CollapsingHeader("状态", ImGuiTreeNodeFlags_DefaultOpen))
            rows(data.states, "状态", [&](auto& state) {
                const auto previous = state.name;
                if(Ui::input_text("名称", state.name)) {
                    if(data.initial == previous)
                        data.initial = state.name;
                    for(auto& transition : data.transitions) {
                        if(!previous.empty() && transition.from == previous)
                            transition.from = state.name;
                        if(transition.to == previous)
                            transition.to = state.name;
                    }
                }
                ImGui::Checkbox("循环", &state.loop);
                if(ImGui::BeginCombo(
                       "混合参数", state.parameter.empty() ? "单片段" : state.parameter.c_str())) {
                    if(ImGui::Selectable("单片段", state.parameter.empty()))
                        state.parameter.clear();
                    for(const auto& [name, value] : data.parameters)
                        if(std::holds_alternative<float>(value)
                            && ImGui::Selectable(name.c_str(), name == state.parameter))
                            state.parameter = name;
                    ImGui::EndCombo();
                }
                if(!state.parameter.empty())
                    ImGui::DragFloat("参数平滑 (s)", &state.smoothing, .01f, 0, 10);
                rows(state.motions, "片段", [&](auto& motion) {
                    Ui::input_text("片段名", motion.clip);
                    if(!state.parameter.empty())
                        ImGui::DragFloat("阈值", &motion.threshold, .1f);
                });
            });
        if(ImGui::CollapsingHeader("过渡", ImGuiTreeNodeFlags_DefaultOpen))
            rows(data.transitions, "过渡", [&](auto& transition) {
                state_choice("来源", transition.from, data, true);
                state_choice("目标", transition.to, data);
                ImGui::DragFloat("过渡时间 (s)", &transition.duration, .01f, 0, 10);
                ImGui::Checkbox("播放完成一次后", &transition.after_finished);
                rows(transition.conditions, "条件", [&](auto& condition) {
                    if(ImGui::BeginCombo("参数", condition.parameter.c_str())) {
                        for(const auto& [name, value] : data.parameters)
                            if(ImGui::Selectable(name.c_str(), name == condition.parameter)) {
                                condition.parameter = name;
                                condition.value = value;
                                condition.compare =
                                    Comet::AnimationController::Condition::Compare::Equal;
                            }
                        ImGui::EndCombo();
                    }
                    constexpr std::array labels{"==", "!=", ">", ">=", "<", "<="};
                    const int count = std::holds_alternative<bool>(condition.value) ? 2 : 6;
                    int selected = int(condition.compare);
                    if(ImGui::Combo("比较", &selected, labels.data(), count))
                        condition.compare =
                            Comet::AnimationController::Condition::Compare(selected);
                    scalar("值", condition.value);
                });
            });
        const auto valid = data.validate();
        if(!valid)
            ImGui::TextWrapped("%s", valid.error().c_str());
        const bool dirty = data != *m_controller_saved;
        ImGui::BeginDisabled(!valid || !dirty);
        if(ImGui::Button("保存控制器###Save Controller"))
            m_asset_edit =
                AssetEdit{m_loaded_target, AnimationControllerEdit{*m_controller_saved, data}};
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("放弃修改###Discard Controller"))
            data = *m_controller_saved;
        if(m_next_asset && dirty)
            ImGui::OpenPopup("未保存的动画控制器");
        if(ImGui::BeginPopupModal(
               "未保存的动画控制器", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("切换前保存当前控制器？");
            ImGui::BeginDisabled(!valid);
            if(ImGui::Button("保存并切换")) {
                m_asset_edit =
                    AssetEdit{m_loaded_target, AnimationControllerEdit{*m_controller_saved, data}};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("放弃并切换")) {
                const auto next = *m_next_asset;
                data = *m_controller_saved;
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
    }
}
