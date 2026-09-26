#include "ui/shortcut_settings_dialog.h"

#include "ui/language.h"

#include <array>
#include <cctype>
#include <cstring>
#include <string_view>
#include <utility>
#include <imgui.h>

namespace CometEditor {
    namespace {
        constexpr std::array<const char*, EditorShortcuts::ACTION_COUNT> ACTION_LABELS{
            "New Scene", "Open Scene", "Save Scene", "Undo", "Redo", "Copy Entity",
            "Paste Entity", "Delete Selection", "Focus Selection"};

        void input_text(const char* label, std::string& value) {
            const bool changed = ImGui::InputText(label, value.data(), value.capacity() + 1,
                ImGuiInputTextFlags_CallbackResize,
                [](ImGuiInputTextCallbackData* data) {
                    auto& text = *static_cast<std::string*>(data->UserData);
                    text.resize(static_cast<std::size_t>(data->BufTextLen));
                    data->Buf = text.data();
                    return 0;
                },
                &value);
            if(changed)
                value.resize(std::strlen(value.c_str()));
        }

        std::string_view trim(std::string_view text) {
            while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);
            return text;
        }
    }

    void ShortcutSettingsDialog::load_draft(const EditorShortcuts& shortcuts) {
        const auto bindings = shortcuts.binding_texts();
        for(std::size_t index = 0; index < bindings.size(); ++index) {
            auto& text = m_draft[index];
            text.clear();
            for(const auto& binding : bindings[index]) {
                if(!text.empty())
                    text += ", ";
                text += binding;
            }
        }
    }

    void ShortcutSettingsDialog::request(const EditorShortcuts& current) {
        load_draft(current);
        m_request.reset();
        m_error.clear();
        m_active = true;
        m_open_requested = true;
        m_close_requested = false;
    }

    Comet::Result<EditorShortcuts> ShortcutSettingsDialog::build_shortcuts() const {
        using Result = Comet::Result<EditorShortcuts>;
        EditorShortcuts::BindingTexts bindings;
        for(std::size_t index = 0; index < m_draft.size(); ++index) {
            auto remaining = trim(m_draft[index]);
            if(remaining.empty())
                continue;
            while(true) {
                const auto separator = remaining.find(',');
                const auto chord = trim(remaining.substr(0, separator));
                if(chord.empty())
                    return Result::failure(
                        std::string(EditorShortcuts::action_name(
                            static_cast<EditorShortcuts::Action>(index)))
                        + ": empty shortcut");
                bindings[index].emplace_back(chord);
                if(separator == std::string_view::npos)
                    break;
                remaining.remove_prefix(separator + 1);
            }
        }
        return EditorShortcuts::from_texts(std::move(bindings));
    }

    void ShortcutSettingsDialog::render() {
        if(!m_active)
            return;
        constexpr const char* title = "Keyboard Shortcuts";
        if(m_open_requested) {
            ImGui::OpenPopup(title);
            m_open_requested = false;
        }
        if(!ImGui::BeginPopupModal(Ui::label(title).c_str(), nullptr,
               ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if(m_close_requested) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            m_active = false;
            m_close_requested = false;
            return;
        }

        ImGui::TextUnformatted(
            Ui::text("Separate alternatives with commas; empty disables a shortcut."));
        ImGui::TextUnformatted(Ui::text("Primary means Cmd on macOS and Ctrl elsewhere."));
        for(std::size_t index = 0; index < m_draft.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::TextUnformatted(Ui::text(ACTION_LABELS[index]));
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(330.0f);
            input_text("##Shortcut", m_draft[index]);
            ImGui::PopID();
        }
        if(ImGui::Button(Ui::label("Restore Defaults").c_str())) {
            load_draft(EditorShortcuts{});
            m_error.clear();
        }
        ImGui::Separator();
        if(ImGui::Button(Ui::label("Save").c_str())) {
            auto shortcuts = build_shortcuts();
            if(shortcuts) {
                m_request = std::move(shortcuts).value();
                m_error.clear();
            } else {
                m_error = shortcuts.error();
            }
        }
        ImGui::SameLine();
        if(ImGui::Button(Ui::label("Cancel").c_str())) {
            ImGui::CloseCurrentPopup();
            m_active = false;
            m_request.reset();
            m_error.clear();
        }
        if(!m_error.empty())
            ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::EndPopup();
    }

    std::optional<EditorShortcuts> ShortcutSettingsDialog::take_request() {
        return std::exchange(m_request, std::nullopt);
    }

    void ShortcutSettingsDialog::complete(const Comet::Result<void>& result) {
        m_close_requested = static_cast<bool>(result);
        m_error = result ? std::string{} : result.error();
    }
}
