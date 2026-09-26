#include "ui/shortcuts.h"
#include "common/file_io.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace CometEditor {
    namespace {
        constexpr std::array<std::string_view, 9> ACTION_NAMES{"scene.new", "scene.open",
            "scene.save", "edit.undo", "edit.redo", "edit.copy_entity", "edit.paste_entity",
            "edit.delete_selection", "viewport.focus_selection"};

        ImGuiKey parse_key(const std::string_view name) {
            if(name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
                return static_cast<ImGuiKey>(ImGuiKey_A + name[0] - 'A');
            if(name.size() == 1 && name[0] >= '0' && name[0] <= '9')
                return static_cast<ImGuiKey>(ImGuiKey_0 + name[0] - '0');
            for(int index = 1; index <= 12; ++index) {
                if(name == "F" + std::to_string(index))
                    return static_cast<ImGuiKey>(ImGuiKey_F1 + index - 1);
            }
            constexpr std::pair<std::string_view, ImGuiKey> keys[]{{"Space", ImGuiKey_Space},
                {"Enter", ImGuiKey_Enter}, {"Tab", ImGuiKey_Tab}, {"Backspace", ImGuiKey_Backspace},
                {"Delete", ImGuiKey_Delete}, {"Insert", ImGuiKey_Insert}, {"Home", ImGuiKey_Home},
                {"End", ImGuiKey_End}, {"PageUp", ImGuiKey_PageUp}, {"PageDown", ImGuiKey_PageDown},
                {"Left", ImGuiKey_LeftArrow}, {"Right", ImGuiKey_RightArrow},
                {"Up", ImGuiKey_UpArrow}, {"Down", ImGuiKey_DownArrow}, {"Comma", ImGuiKey_Comma},
                {"Period", ImGuiKey_Period}};
            for(const auto& [text, key] : keys) {
                if(text == name)
                    return key;
            }
            return ImGuiKey_None;
        }
    }

    Comet::Result<EditorShortcuts::Binding> EditorShortcuts::parse_binding(std::string_view text) {
        using Result = Comet::Result<Binding>;
        ImGuiKeyChord modifiers = 0;
        // ImGui 根据 ConfigMacOSXBehaviors 把此处的 Ctrl 映射为 Cmd。
        while(text.find('+') != std::string_view::npos) {
            const auto separator = text.find('+');
            const auto token = text.substr(0, separator);
            ImGuiKeyChord modifier = 0;
            if(token == "Primary")
                modifier = ImGuiMod_Ctrl;
            else if(token == "Shift")
                modifier = ImGuiMod_Shift;
            else if(token == "Alt")
                modifier = ImGuiMod_Alt;
            else
                return Result::failure("Unknown modifier '" + std::string(token)
                                       + "'; expected Primary, Shift or Alt");
            if(modifiers & modifier)
                return Result::failure("Duplicate shortcut modifier");
            modifiers |= modifier;
            text.remove_prefix(separator + 1);
        }
        const auto key = parse_key(text);
        if(key == ImGuiKey_None)
            return Result::failure("Unknown or reserved key '" + std::string(text)
                                   + "' (Escape is reserved for cancellation)");
        return Result::success({modifiers | key, std::string(text)});
    }

    EditorShortcuts::EditorShortcuts() {
        const std::array<Binding, 9> defaults{{{ImGuiMod_Ctrl | ImGuiKey_N, "N"},
            {ImGuiMod_Ctrl | ImGuiKey_O, "O"}, {ImGuiMod_Ctrl | ImGuiKey_S, "S"},
            {ImGuiMod_Ctrl | ImGuiKey_Z, "Z"}, {ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, "Z"},
            {ImGuiMod_Ctrl | ImGuiKey_C, "C"}, {ImGuiMod_Ctrl | ImGuiKey_V, "V"},
            {ImGuiMod_Ctrl | ImGuiKey_Backspace, "Backspace"}, {ImGuiKey_F, "F"}}};
        for(std::size_t index = 0; index < defaults.size(); ++index)
            m_bindings[index].push_back(defaults[index]);
        m_bindings[static_cast<std::size_t>(Action::Redo)].push_back(
            {ImGuiMod_Ctrl | ImGuiKey_Y, "Y"});
    }

    Comet::Result<EditorShortcuts> EditorShortcuts::load(const std::filesystem::path& path) {
        using Result = Comet::Result<EditorShortcuts>;
        auto text = Comet::read_text_file(path);
        if(!text)
            return Result::failure(text.error());
        auto result = parse(text.value());
        if(!result)
            return Result::failure(
                "Invalid shortcut config '" + path.string() + "': " + result.error());
        return result;
    }

    Comet::Result<EditorShortcuts> EditorShortcuts::parse(const std::string_view yaml) {
        using Result = Comet::Result<EditorShortcuts>;
        EditorShortcuts result;
        YAML::Node root;
        try {
            root = YAML::Load(std::string(yaml));
        } catch(const YAML::Exception& error) {
            return Result::failure(error.what());
        }
        if(!root || root.IsNull())
            return Result::success(std::move(result));
        if(!root.IsMap())
            return Result::failure("Expected a config mapping");
        const YAML::Node editor = root["editor"];
        if(!editor)
            return Result::success(std::move(result));
        if(!editor.IsMap())
            return Result::failure("editor must be a mapping");
        const YAML::Node shortcuts = editor["shortcuts"];
        if(!shortcuts)
            return Result::success(std::move(result));
        if(!shortcuts.IsMap())
            return Result::failure("editor.shortcuts must be a mapping");

        std::unordered_set<std::string> configured;
        for(const auto& entry : shortcuts) {
            if(!entry.first.IsScalar())
                return Result::failure("Shortcut action must be a string");
            const std::string name = entry.first.Scalar();
            const auto action = std::ranges::find(ACTION_NAMES, name);
            if(action == ACTION_NAMES.end() || !configured.insert(name).second)
                return Result::failure("Unknown or duplicate shortcut action: " + name);
            if(!entry.second.IsSequence())
                return Result::failure("editor.shortcuts." + name + " must be a list");
            auto& bindings = result.m_bindings[action - ACTION_NAMES.begin()];
            bindings.clear();
            for(const auto& chord : entry.second) {
                const auto location = "editor.shortcuts." + name + ": ";
                if(!chord.IsScalar())
                    return Result::failure(location + "binding must be a string");
                auto binding = parse_binding(chord.Scalar());
                if(!binding)
                    return Result::failure(location + binding.error());
                bindings.push_back(std::move(binding).value());
            }
        }

        // 全局与视口快捷键可能同时生效，需一起检查冲突。
        std::unordered_map<ImGuiKeyChord, std::string_view> owners;
        for(std::size_t index = 0; index < result.m_bindings.size(); ++index) {
            for(const auto& binding : result.m_bindings[index]) {
                const auto [owner, inserted] = owners.emplace(binding.chord, ACTION_NAMES[index]);
                if(!inserted)
                    return Result::failure("Shortcut conflict between " + std::string(owner->second)
                                           + " and " + std::string(ACTION_NAMES[index]));
            }
        }
        return Result::success(std::move(result));
    }

    bool EditorShortcuts::pressed(const Action action, const ImGuiInputFlags flags) const {
        bool triggered = false;
        for(const auto& binding : m_bindings.at(static_cast<std::size_t>(action))) {
            // 不短路，确保每个备选绑定都参与 ImGui 输入路由。
            triggered |= ImGui::Shortcut(binding.chord, flags);
        }
        return triggered;
    }

    std::string EditorShortcuts::label(const Action action, const bool mac) const {
        std::string result;
        for(const auto& binding : m_bindings.at(static_cast<std::size_t>(action))) {
            if(!result.empty())
                result += " / ";
            if(binding.chord & ImGuiMod_Ctrl)
                result += mac ? "Cmd+" : "Ctrl+";
            if(binding.chord & ImGuiMod_Alt)
                result += mac ? "Option+" : "Alt+";
            if(binding.chord & ImGuiMod_Shift)
                result += "Shift+";
            result += binding.key_name;
        }
        return result;
    }
}
