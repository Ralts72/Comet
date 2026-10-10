#include "project/player_input_panel.h"
#include "project/input_widgets.h"
#include "ui/widgets.h"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>
#include <utility>

namespace CometEditor {
    namespace {
        using Input = Comet::Input;
        using Actions = Comet::InputActions;
        using Overrides = Comet::InputOverrides;

        float button_width(const char* caption) {
            return ImGui::CalcTextSize(caption, nullptr, true).x
                   + ImGui::GetStyle().FramePadding.x * 2;
        }

        float field_width(float width, const char* caption) {
            return width + ImGui::CalcTextSize(caption, nullptr, true).x
                   + ImGui::GetStyle().ItemInnerSpacing.x;
        }

        void set_field_width(float width, const char* caption) {
            const auto remaining = ImGui::GetContentRegionAvail().x
                                   - ImGui::CalcTextSize(caption, nullptr, true).x
                                   - ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetNextItemWidth(std::max(1.f, std::min(width, remaining)));
        }

        void same_line_if_fits(float width) {
            const auto right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            if(ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right)
                ImGui::SameLine();
        }

        float footer_height() {
            const auto& style = ImGui::GetStyle();
            const auto available = std::max(1.f, ImGui::GetContentRegionAvail().x);
            float used = 0;
            int rows = 1;
            for(const auto caption : {"应用", "取消", "全部恢复默认"}) {
                const auto width = std::min(available, button_width(caption));
                if(used > 0 && used + style.ItemSpacing.x + width > available) {
                    ++rows;
                    used = 0;
                }
                used += (used > 0 ? style.ItemSpacing.x : 0) + width;
            }
            return rows * ImGui::GetFrameHeight() + (rows - 1) * style.ItemSpacing.y;
        }

        bool binding_rejected(
            const Overrides::Resolution& resolved, Comet::Uuid action, Comet::Uuid binding) {
            return std::ranges::any_of(resolved.issues, [&](const auto& issue) {
                return issue.action == action && (!issue.binding || issue.binding == binding);
            });
        }

        bool is_reserved(
            const Actions::Control& control, std::span<const Input::Key> reserved_keys) {
            const auto* key = std::get_if<Input::Key>(&control);
            return key && std::ranges::find(reserved_keys, *key) != reserved_keys.end();
        }

        std::optional<Actions::Control> first_control(std::string_view source,
            const Actions::Control& default_control, std::span<const Input::Key> reserved_keys) {
            const auto controls = Ui::input_controls(source);
            if(controls.empty())
                return std::nullopt;
            if(std::ranges::find(controls, default_control) != controls.end()
                && !is_reserved(default_control, reserved_keys))
                return default_control;
            auto preferred = controls.front();
            if(source == "key")
                preferred = Input::Key::Space;
            if(!is_reserved(preferred, reserved_keys))
                return preferred;
            for(const auto& control : controls)
                if(!is_reserved(control, reserved_keys))
                    return control;
            return std::nullopt;
        }

        void render_reserved_keys(std::span<const Input::Key> reserved_keys) {
            if(reserved_keys.empty())
                return;
            std::string names;
            for(const auto key : reserved_keys) {
                if(!names.empty())
                    names += ", ";
                const auto name = Actions::format_binding({key}).value();
                names += name.control.c_str();
            }
            ImGui::TextWrapped("%s %s", "程序保留键：", names.c_str());
        }

        std::optional<Actions::Control> control_choices(const Actions::Control& current,
            std::string_view source, std::span<const Input::Key> reserved_keys) {
            std::optional<Actions::Control> chosen;
            for(const auto& value : Ui::input_controls(source)) {
                const auto name = Actions::format_binding({value}).value();
                const bool selected = current == value;
                ImGui::BeginDisabled(is_reserved(value, reserved_keys));
                if(ImGui::Selectable(name.control.c_str(), selected))
                    chosen = value;
                if(selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::EndDisabled();
            }
            return chosen;
        }

        Actions::Binding composed_binding(
            const Actions::Binding& binding, const Overrides::Binding& patch) {
            auto composed = binding;
            if(patch.control)
                composed.control = *patch.control;
            if(patch.scale)
                composed.scale = *patch.scale;
            if(patch.deadzone)
                composed.deadzone = *patch.deadzone;
            return composed;
        }

        void render_disabled_binding(const Actions::Action& action, const Actions::Binding& binding,
            const Overrides::Binding& patch) {
            const auto retained = composed_binding(binding, patch);
            const auto name = Actions::format_binding(retained).value();
            ImGui::TextWrapped("%s %s / %s", "已禁用绑定（当前不生效）：",
                Ui::input_source_name(name.source), name.control.c_str());
            if(action.type != Actions::Type::Button || patch.scale)
                ImGui::TextWrapped("%s: %.3f", "倍率", retained.scale);
            if(name.source == "gamepad_axis" || patch.deadzone)
                ImGui::TextWrapped("%s: %.3f", "死区", retained.deadzone);
        }
    }

    void PlayerInputPanel::open(const Actions& defaults, const Overrides& current,
        std::span<const Input::Key> reserved_keys) {
        if(m_open)
            return;
        m_edit.reset(defaults, current, reserved_keys);
        m_selected_action = 0;
        m_action_filter.clear();
        m_open = true;
        m_open_requested = true;
        m_close_requested = false;
    }

    void PlayerInputPanel::close() {
        if(m_open)
            m_close_requested = true;
        m_open = false;
        m_open_requested = false;
        m_action_filter.clear();
        m_edit.clear();
    }

    void PlayerInputPanel::render_controls(const Action& action, const Binding& binding,
        const Binding& effective, const Input::Frame& input) {
        const auto name = Actions::format_binding(effective).value();
        if(m_edit.is_reserved(effective.control))
            ImGui::TextWrapped("%s", "此绑定使用程序保留键，不会传递给游戏。");
        set_field_width(145, "来源");
        if(ImGui::BeginCombo("来源###Source", Ui::input_source_name(name.source))) {
            for(const auto source : Ui::input_sources(action.type)) {
                if(ImGui::Selectable(Ui::input_source_label(source).c_str(), name.source == source)
                    && name.source != source) {
                    if(const auto control =
                            first_control(source, binding.control, m_edit.reserved_keys()))
                        m_edit.change_control(action.id, binding.id, *control);
                    else
                        m_edit.report_error("此输入来源没有可用的控制。");
                    m_edit.cancel_capture();
                }
            }
            ImGui::EndCombo();
        }
        same_line_if_fits(field_width(150, "按键／轴"));
        set_field_width(150, "按键／轴");
        if(ImGui::BeginCombo("按键／轴###Control", name.control.c_str())) {
            if(const auto chosen =
                    control_choices(effective.control, name.source, m_edit.reserved_keys())) {
                m_edit.change_control(action.id, binding.id, *chosen);
                m_edit.cancel_capture();
            }
            ImGui::EndCombo();
        }
        using CaptureKind = Comet::PlayerInputEdit::CaptureKind;
        if(name.source == "key")
            render_capture_button(action, binding, CaptureKind::Keyboard, input);
        else if(name.source == "gamepad_button")
            render_capture_button(action, binding, CaptureKind::GamepadButton, input);
        if(action.type != Actions::Type::Button) {
            auto scale = effective.scale;
            set_field_width(120, "倍率");
            if(ImGui::InputFloat("倍率###Multiplier", &scale, 0, 0, "%.3f")) {
                m_edit.change_scale(action.id, binding.id, scale);
            }
        }
        if(name.source == "gamepad_axis") {
            same_line_if_fits(field_width(120, "死区"));
            auto deadzone = effective.deadzone;
            set_field_width(120, "死区");
            if(ImGui::InputFloat("死区###Deadzone", &deadzone, 0, 0, "%.3f")) {
                m_edit.change_deadzone(action.id, binding.id, deadzone);
            }
        }
    }

    void PlayerInputPanel::render_capture_button(const Action& action, const Binding& binding,
        const Comet::PlayerInputEdit::CaptureKind kind, const Input::Frame& input) {
        const auto& capture = m_edit.capture();
        const bool capturing =
            capture && capture->action == action.id && capture->binding == binding.id;
        const char* caption = capturing ? "请按键###Press Key" : "录入按键###Record Key";
        if(kind == Comet::PlayerInputEdit::CaptureKind::GamepadButton)
            caption = capturing ? "请按按钮###Press Button" : "录入按钮###Record Button";
        same_line_if_fits(button_width(caption));
        if(!ImGui::Button(caption))
            return;
        m_edit.start_capture(action.id, binding.id, input, kind);
        if(m_edit.capture())
            ImGui::ClearActiveID();
    }

    void PlayerInputPanel::render_binding(const Action& action, const Binding& binding,
        Overrides::Resolution& resolved, const Input::Frame& input) {
        ImGui::PushID(binding.id.to_string().c_str());
        auto patch = m_edit.binding_patch(action.id, binding.id);
        const auto* action_override = m_edit.action_patch(action.id);
        const bool incompatible = action_override && action_override->type != action.type;
        bool changed = false;
        bool disabled = patch.disabled && !incompatible;
        if(ImGui::Checkbox("禁用绑定###Disable Binding", &disabled)) {
            m_edit.disable_binding(action.id, binding.id, disabled);
            changed = true;
        }
        same_line_if_fits(button_width("恢复绑定默认值"));
        if(ImGui::Button("恢复绑定默认值###Restore Binding")) {
            m_edit.restore_binding(action.id, binding.id);
            changed = true;
        }
        if(changed) {
            auto updated = m_edit.resolution();
            if(updated)
                resolved = std::move(updated).value();
        }
        patch = m_edit.binding_patch(action.id, binding.id);
        if(patch.disabled && !incompatible) {
            render_disabled_binding(action, binding, patch);
            ImGui::Separator();
            ImGui::PopID();
            return;
        }
        const bool overridden = patch.control || patch.scale || patch.deadzone;
        const bool rejected = binding_rejected(resolved, action.id, binding.id);
        const char* status = "继承项目默认";
        if(rejected)
            status = "覆盖未生效；使用项目默认";
        else if(overridden)
            status = "个人覆盖";
        ImGui::TextWrapped("%s", status);
        const auto& effective_bindings = resolved.actions.actions()[m_selected_action].bindings;
        const auto effective = std::ranges::find(effective_bindings, binding.id, &Binding::id);
        ImGui::BeginDisabled(rejected);
        const auto& effective_binding =
            effective == effective_bindings.end() ? binding : *effective;
        render_controls(action, binding, effective_binding, input);
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::PopID();
    }

    void PlayerInputPanel::render_action_selector() {
        const auto& actions = m_edit.defaults().actions();
        set_field_width(250, "筛选动作");
        Ui::input_text("筛选动作###Filter Actions", m_action_filter);
        if(!m_action_filter.empty()) {
            same_line_if_fits(button_width("清除筛选"));
            if(ImGui::Button("清除筛选###Clear Filter"))
                m_action_filter.clear();
        }
        set_field_width(250, "动作");
        if(ImGui::BeginCombo("动作###Action", actions[m_selected_action].name.c_str())) {
            bool found = false;
            for(std::size_t index = 0; index < actions.size(); ++index) {
                if(!m_action_filter.empty()
                    && !ImStristr(
                        actions[index].name.c_str(), nullptr, m_action_filter.c_str(), nullptr))
                    continue;
                found = true;
                ImGui::PushID(actions[index].id.to_string().c_str());
                if(ImGui::Selectable(actions[index].name.c_str(), m_selected_action == index)) {
                    m_selected_action = index;
                    m_edit.cancel_capture();
                }
                ImGui::PopID();
            }
            if(!found)
                ImGui::TextDisabled("%s", "没有匹配的动作。");
            ImGui::EndCombo();
        }
    }

    void PlayerInputPanel::render_actions(const Input::Frame& input) {
        const auto& actions = m_edit.defaults().actions();
        if(actions.empty()) {
            ImGui::TextDisabled("%s", "项目没有配置输入动作。");
            return;
        }
        render_action_selector();
        const auto& action = actions[m_selected_action];
        same_line_if_fits(ImGui::CalcTextSize(Ui::input_type_name(action.type)).x);
        ImGui::TextDisabled("%s", Ui::input_type_name(action.type));
        const auto* patch = m_edit.action_patch(action.id);
        bool incompatible = patch && patch->type != action.type;
        bool disabled = !incompatible && patch && patch->disabled;
        ImGui::BeginDisabled(incompatible);
        if(ImGui::Checkbox("禁用动作###Disable Action", &disabled))
            m_edit.disable_action(action.id, disabled);
        ImGui::EndDisabled();
        same_line_if_fits(button_width("恢复动作默认值"));
        if(ImGui::Button("恢复动作默认值###Restore Action"))
            m_edit.restore_action(action.id);
        patch = m_edit.action_patch(action.id);
        incompatible = patch && patch->type != action.type;
        disabled = !incompatible && patch && patch->disabled;
        if(incompatible)
            ImGui::TextWrapped("%s", "该动作的类型已变化，请先恢复默认值再编辑。");
        if(disabled) {
            ImGui::TextWrapped("%s", "已禁用；个人配置仍保留，重新启用后恢复。");
            return;
        }
        auto resolved = m_edit.resolution();
        if(!resolved)
            return;
        ImGui::BeginDisabled(incompatible);
        ImGui::BeginChild("Bindings", ImVec2(0, 190), true);
        for(const auto& binding : action.bindings)
            render_binding(action, binding, resolved.value(), input);
        ImGui::EndChild();
        ImGui::EndDisabled();
    }

    void PlayerInputPanel::render_feedback() {
        const auto resolved = m_edit.resolution();
        if(!resolved) {
            ImGui::TextWrapped("%s", resolved.error().c_str());
            return;
        }
        if(ImGui::CollapsingHeader(
               "绑定关系###Binding Relationships", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("Relationships", ImVec2(0, 120), true);
            Ui::render_binding_relationships(resolved.value().actions, m_selected_action);
            ImGui::EndChild();
        }
        render_diagnostics(resolved.value());
    }

    void PlayerInputPanel::render_diagnostics(const Overrides::Resolution& resolved) {
        if(resolved.issues.empty())
            return;
        ImGui::TextWrapped("%s", "不兼容的个人配置暂不生效，恢复默认前仍保留。");
        ImGui::BeginChild("Diagnostics", ImVec2(0, 75), true);
        for(const auto& issue : resolved.issues) {
            ImGui::PushID(issue.action.to_string().c_str());
            ImGui::PushID(issue.binding.to_string().c_str());
            ImGui::TextWrapped("%s", issue.message.c_str());
            if(ImGui::Button("移除此覆盖###Remove Override")) {
                if(issue.binding)
                    m_edit.restore_binding(issue.action, issue.binding);
                else
                    m_edit.restore_action(issue.action);
            }
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled(
                "%s / %s", issue.action.to_string().c_str(), issue.binding.to_string().c_str());
            ImGui::PopTextWrapPos();
            ImGui::Separator();
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void PlayerInputPanel::render_error() {
        if(m_edit.error().empty())
            return;
        const auto available = ImGui::GetContentRegionAvail().y - footer_height()
                               - ImGui::GetStyle().ItemSpacing.y * 2;
        const auto error_height = std::min(96.f, std::max(1.f, available * 0.35f));
        ImGui::BeginChild("Error", ImVec2(0, error_height), true);
        ImGui::TextWrapped("%s", m_edit.error().c_str());
        ImGui::EndChild();
    }

    void PlayerInputPanel::render_content(const Input::Frame& input) {
        const auto body_height = std::max(1.f,
            ImGui::GetContentRegionAvail().y - footer_height() - ImGui::GetStyle().ItemSpacing.y);
        ImGui::BeginChild("Content", ImVec2(0, body_height));
        ImGui::TextWrapped(
            "%s", "仅修改玩家个人配置，不改变项目默认值；未修改的字段继续使用默认值。");
        render_actions(input);
        if(!m_edit.waiting()) {
            const bool editing_text = ImGui::GetInputTextState(ImGui::GetActiveID());
            const bool panel_focused =
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            m_edit.capture_input(input, !editing_text && panel_focused);
        }
        if(m_edit.capture()) {
            const char* prompt = "请按下新按键；Esc 取消录入。";
            if(m_edit.capture()->gamepad)
                prompt = "请按下手柄按钮；Esc 取消录入。";
            ImGui::TextWrapped("%s", prompt);
        }
        render_feedback();
        ImGui::EndChild();
    }

    void PlayerInputPanel::render_footer() {
        if(ImGui::Button("应用###Apply"))
            m_edit.apply();
        same_line_if_fits(button_width("取消"));
        if(ImGui::Button("取消###Cancel"))
            close();
        same_line_if_fits(button_width("全部恢复默认"));
        if(ImGui::Button("全部恢复默认###Restore All"))
            m_edit.restore_all();
    }

    bool PlayerInputPanel::render(const Input::Frame& input) {
        if(!m_open && !m_close_requested)
            return false;
        const auto title = std::string("玩家输入设置###Player Input");
        const bool opening = m_open_requested;
        if(m_open_requested) {
            ImGui::OpenPopup(title.c_str());
            m_open_requested = false;
        }
        Ui::set_next_input_modal_bounds(title.c_str(), opening, ImVec2(800, 550), ImVec2(320, 240));
        if(!ImGui::BeginPopupModal(title.c_str(), nullptr,
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                   | ImGuiWindowFlags_NoScrollWithMouse)) {
            m_close_requested = false;
            return true;
        }
        if(m_open && !m_edit.waiting()) {
            const bool capturing = m_edit.capture().has_value();
            // Enter 可能在绘制数值框时结束编辑，先保留其本帧输入归属。
            if(ImGui::GetInputTextState(ImGui::GetActiveID()))
                m_edit.cancel_capture();
            if(!capturing && input.focused && input.key(Input::Key::Escape).pressed)
                close();
        }
        if(m_open) {
            render_reserved_keys(m_edit.reserved_keys());
            render_error();
            ImGui::BeginDisabled(m_edit.waiting());
            render_content(input);
            render_footer();
            ImGui::EndDisabled();
        }
        if(m_close_requested) {
            ImGui::CloseCurrentPopup();
            m_close_requested = false;
        }
        ImGui::EndPopup();
        return true;
    }

    std::optional<Overrides> PlayerInputPanel::take_request() {
        return m_edit.take_request();
    }

    void PlayerInputPanel::complete(const Comet::Result<void>& result) {
        if(m_edit.complete(result))
            close();
    }
}
