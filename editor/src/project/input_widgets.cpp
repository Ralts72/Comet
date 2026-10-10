#include "project/input_widgets.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <imgui_internal.h>

namespace CometEditor::Ui {
    namespace {
        using Actions = Comet::InputActions;
        using BindingRelation = Actions::BindingRelation;

        template<typename Control, int First, int Last> constexpr auto make_controls() {
            std::array<Actions::Control, Last - First> controls{};
            for(int index = First; index < Last; ++index)
                controls[index - First] = static_cast<Control>(index);
            return controls;
        }

        const Actions::Context* action_context(
            const Actions& actions, const Actions::Action& action) {
            const auto& contexts = actions.contexts();
            const auto found = std::ranges::find(contexts, action.context, &Actions::Context::name);
            return found == contexts.end() ? nullptr : &*found;
        }

        const char* relation_name(const BindingRelation relation, const bool common) {
            switch(relation) {
                case BindingRelation::Shared:
                    if(common)
                        return "双方共享（公共动作不参与消费）";
                    return "双方共享";
                case BindingRelation::Consumes:
                    return "本绑定消费对方的同一输入";
                case BindingRelation::ConsumedBy:
                    return "本绑定的同一输入被对方消费";
                case BindingRelation::Unrelated:
                    return "没有与其他动作重叠的绑定。";
            }
            return "未知";
        }

        void render_relationship(const Actions::Binding& binding, const Actions::Action& other,
            const Actions::Context* context, const Actions::Context* other_context,
            const BindingRelation relation) {
            const auto control = Actions::format_binding(binding);
            if(!control)
                return;
            const char* group = "公共（始终启用）";
            if(other_context)
                group = other_context->name.c_str();
            ImGui::Separator();
            ImGui::TextWrapped("%s/%s: %s [%s]", control.value().source.data(),
                control.value().control.c_str(), other.name.c_str(), group);
            ImGui::TextWrapped("%s", relation_name(relation, !context || !other_context));
            if(context && !context->enabled)
                ImGui::TextWrapped("%s %s", "默认关闭：", context->name.c_str());
            if(other_context && other_context != context && !other_context->enabled)
                ImGui::TextWrapped("%s %s", "默认关闭：", other_context->name.c_str());
        }
    }

    const char* input_type_name(const Actions::Type type) {
        switch(type) {
            case Actions::Type::Button:
                return "按钮";
            case Actions::Type::Axis:
                return "轴";
            case Actions::Type::Delta:
                return "位移";
        }
        return "未知";
    }

    const char* input_type_label(const Actions::Type type) {
        switch(type) {
            case Actions::Type::Button:
                return "按钮###Button";
            case Actions::Type::Axis:
                return "轴###Axis";
            case Actions::Type::Delta:
                return "位移###Delta";
        }
        return "未知";
    }

    std::span<const std::string_view> input_sources(const Actions::Type type) {
        static constexpr std::array<std::string_view, 5> sources{
            "key", "mouse_button", "gamepad_button", "gamepad_axis", "motion"};
        const std::span<const std::string_view> all = sources;
        switch(type) {
            case Actions::Type::Button:
                return all.first(3);
            case Actions::Type::Axis:
                return all.first(4);
            case Actions::Type::Delta:
                return all.last(1);
        }
        return {};
    }

    const char* input_source_name(const std::string_view source) {
        if(source == "key")
            return "键盘";
        if(source == "mouse_button")
            return "鼠标按钮";
        if(source == "gamepad_button")
            return "手柄按钮";
        if(source == "gamepad_axis")
            return "手柄轴";
        if(source == "motion")
            return "鼠标位移／滚轮";
        return "未知";
    }

    std::string input_source_label(const std::string_view source) {
        return std::string(input_source_name(source)) + "###" + std::string(source);
    }

    std::span<const Actions::Control> input_controls(const std::string_view source) {
        using Input = Comet::Input;
        static constexpr auto keys =
            make_controls<Input::Key, int(Input::Key::Unknown) + 1, int(Input::Key::Count)>();
        static constexpr auto mouse_buttons =
            make_controls<Input::MouseButton, 0, int(Input::MouseButton::Count)>();
        static constexpr auto gamepad_buttons =
            make_controls<Input::GamepadButton, 0, int(Input::GamepadButton::Count)>();
        static constexpr auto gamepad_axes =
            make_controls<Input::GamepadAxis, 0, int(Input::GamepadAxis::Count)>();
        static constexpr auto motions =
            make_controls<Actions::Motion, 0, int(Actions::Motion::ScrollY) + 1>();
        if(source == "key")
            return keys;
        if(source == "mouse_button")
            return mouse_buttons;
        if(source == "gamepad_button")
            return gamepad_buttons;
        if(source == "gamepad_axis")
            return gamepad_axes;
        if(source == "motion")
            return motions;
        return {};
    }

    void render_binding_relationships(const Actions& actions, const std::size_t selected_action) {
        if(selected_action >= actions.actions().size())
            return;
        ImGui::TextWrapped("%s", "以下说明双方组都启用时的两两关系，不代表当前运行状态。");
        const auto& selected = actions.actions()[selected_action];
        const auto* context = action_context(actions, selected);
        bool found = false;
        for(auto binding = selected.bindings.begin(); binding != selected.bindings.end();
            ++binding) {
            if(std::any_of(selected.bindings.begin(), binding,
                   [&](const auto& previous) { return previous.control == binding->control; }))
                continue;
            for(std::size_t other_index = 0; other_index < actions.actions().size();
                ++other_index) {
                if(other_index == selected_action)
                    continue;
                const auto& other = actions.actions()[other_index];
                const auto* other_context = action_context(actions, other);
                for(const auto& other_binding : other.bindings) {
                    const auto relation =
                        Actions::compare_bindings(*binding, context, other_binding, other_context);
                    if(relation == BindingRelation::Unrelated)
                        continue;
                    render_relationship(*binding, other, context, other_context, relation);
                    found = true;
                    break;
                }
            }
        }
        if(found)
            ImGui::TextWrapped(
                "%s", "非公共动作仍可能被其他消费组屏蔽；两两共享不代表最终一定可用。");
        else
            ImGui::TextDisabled("%s", "没有与其他动作重叠的绑定。");
    }

    void set_next_input_modal_bounds(const char* title, const bool opening,
        const ImVec2 initial_size, const ImVec2 minimum_size) {
        const auto* viewport = ImGui::GetMainViewport();
        const ImVec2 maximum(
            std::max(1.f, viewport->WorkSize.x - 16), std::max(1.f, viewport->WorkSize.y - 16));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(minimum_size.x, maximum.x), std::min(minimum_size.y, maximum.y)),
            maximum);
        ImGui::SetNextWindowSize(
            ImVec2(std::min(initial_size.x, maximum.x), std::min(initial_size.y, maximum.y)),
            ImGuiCond_Appearing);
        const auto* previous = ImGui::FindWindowByName(title);
        if(previous && !opening) {
            const ImVec2 size(
                std::min(previous->Size.x, maximum.x), std::min(previous->Size.y, maximum.y));
            const ImVec2 minimum(viewport->WorkPos.x + 8, viewport->WorkPos.y + 8);
            ImGui::SetNextWindowPos(
                ImVec2(std::clamp(previous->Pos.x, minimum.x, minimum.x + maximum.x - size.x),
                    std::clamp(previous->Pos.y, minimum.y, minimum.y + maximum.y - size.y)));
        } else {
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                        viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        }
    }

    bool render_player_input_error(std::string& error, const bool close_requested) {
        const auto title = std::string("输入设置错误###Input Settings Error");
        const bool already_open = ImGui::IsPopupOpen(title.c_str());
        if(error.empty() && !already_open)
            return false;
        if(!error.empty() && !already_open)
            ImGui::OpenPopup(title.c_str());

        set_next_input_modal_bounds(
            title.c_str(), !already_open, ImVec2(470, 220), ImVec2(320, 160));
        if(!ImGui::BeginPopupModal(title.c_str(), nullptr,
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                   | ImGuiWindowFlags_NoScrollWithMouse))
            return true;
        if(error.empty()) {
            ImGui::CloseCurrentPopup();
        } else {
            const auto height = std::max(
                1.f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
            ImGui::BeginChild("Content", ImVec2(0, height));
            if(!already_open)
                ImGui::SetScrollY(0);
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::EndChild();
            const auto close = std::string("关闭###Close");
            if(ImGui::Button(close.c_str()) || close_requested) {
                error.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
        return true;
    }
}
