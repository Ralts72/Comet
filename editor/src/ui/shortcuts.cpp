#include "ui/shortcuts.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace CometEditor {
    namespace {
        constexpr std::array<std::string_view, 6> ACTION_NAMES{"scene.new", "scene.open",
            "scene.save", "edit.undo", "edit.redo", "viewport.focus_selection"};

        ImGuiKey parse_key(const std::string_view name) {
            if(name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
                return static_cast<ImGuiKey>(ImGuiKey_A + name[0] - 'A');
            if(name.size() == 1 && name[0] >= '0' && name[0] <= '9')
                return static_cast<ImGuiKey>(ImGuiKey_0 + name[0] - '0');
            for(int index = 1; index <= 12; ++index) {
                if(name == "F" + std::to_string(index))
                    return static_cast<ImGuiKey>(ImGuiKey_F1 + index - 1);
            }
            constexpr std::pair<std::string_view, ImGuiKey> keys[]{
                {"Space", ImGuiKey_Space}, {"Enter", ImGuiKey_Enter},
                {"Tab", ImGuiKey_Tab}, {"Backspace", ImGuiKey_Backspace},
                {"Delete", ImGuiKey_Delete}, {"Insert", ImGuiKey_Insert},
                {"Home", ImGuiKey_Home}, {"End", ImGuiKey_End},
                {"PageUp", ImGuiKey_PageUp}, {"PageDown", ImGuiKey_PageDown},
                {"Left", ImGuiKey_LeftArrow}, {"Right", ImGuiKey_RightArrow},
                {"Up", ImGuiKey_UpArrow}, {"Down", ImGuiKey_DownArrow},
                {"Comma", ImGuiKey_Comma}, {"Period", ImGuiKey_Period}};
            for(const auto& [text, key] : keys) {
                if(text == name)
                    return key;
            }
            throw std::runtime_error("Unknown or reserved key '" + std::string(name)
                                     + "' (Escape is reserved for cancellation)");
        }
    }

    EditorShortcuts::Binding EditorShortcuts::parse_binding(std::string_view text) {
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
                throw std::runtime_error("Unknown modifier '" + std::string(token)
                                         + "'; expected Primary, Shift or Alt");
            if(modifiers & modifier)
                throw std::runtime_error("Duplicate shortcut modifier");
            modifiers |= modifier;
            text.remove_prefix(separator + 1);
        }
        return {modifiers | parse_key(text), std::string(text)};
    }

    EditorShortcuts::EditorShortcuts() {
        constexpr std::array defaults{
            "Primary+N", "Primary+O", "Primary+S", "Primary+Z", "Primary+Shift+Z", "F"};
        for(std::size_t index = 0; index < defaults.size(); ++index)
            m_bindings[index].push_back(parse_binding(defaults[index]));
        m_bindings[static_cast<std::size_t>(Action::Redo)].push_back(
            parse_binding("Primary+Y"));
    }

    EditorShortcuts EditorShortcuts::load(const std::filesystem::path& path) {
        std::ifstream file(path);
        if(!file)
            throw std::runtime_error("Cannot read shortcut config: " + path.string());
        const std::string text{
            std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if(file.bad())
            throw std::runtime_error("Cannot read shortcut config: " + path.string());
        try {
            return parse(text);
        } catch(const std::exception& error) {
            throw std::runtime_error(
                "Invalid shortcut config '" + path.string() + "': " + error.what());
        }
    }

    EditorShortcuts EditorShortcuts::parse(const std::string_view yaml) {
        EditorShortcuts result;
        const YAML::Node root = YAML::Load(std::string(yaml));
        if(!root || root.IsNull())
            return result;
        if(!root.IsMap())
            throw std::runtime_error("Expected a config mapping");
        const YAML::Node editor = root["editor"];
        if(!editor)
            return result;
        if(!editor.IsMap())
            throw std::runtime_error("editor must be a mapping");
        const YAML::Node shortcuts = editor["shortcuts"];
        if(!shortcuts)
            return result;
        if(!shortcuts.IsMap())
            throw std::runtime_error("editor.shortcuts must be a mapping");

        std::unordered_set<std::string> configured;
        for(const auto& entry : shortcuts) {
            const std::string name = entry.first.as<std::string>();
            const auto action = std::ranges::find(ACTION_NAMES, name);
            if(action == ACTION_NAMES.end() || !configured.insert(name).second)
                throw std::runtime_error("Unknown or duplicate shortcut action: " + name);
            if(!entry.second.IsSequence())
                throw std::runtime_error("editor.shortcuts." + name + " must be a list");
            auto& bindings = result.m_bindings[action - ACTION_NAMES.begin()];
            bindings.clear();
            for(const auto& chord : entry.second) {
                try {
                    bindings.push_back(parse_binding(chord.as<std::string>()));
                } catch(const std::exception& error) {
                    throw std::runtime_error(
                        "editor.shortcuts." + name + ": " + error.what());
                }
            }
        }

        // 全局与视口快捷键可能同时生效，需一起检查冲突。
        std::unordered_map<ImGuiKeyChord, std::string_view> owners;
        for(std::size_t index = 0; index < result.m_bindings.size(); ++index) {
            for(const auto& binding : result.m_bindings[index]) {
                const auto [owner, inserted] =
                    owners.emplace(binding.chord, ACTION_NAMES[index]);
                if(!inserted)
                    throw std::runtime_error("Shortcut conflict between "
                                             + std::string(owner->second) + " and "
                                             + std::string(ACTION_NAMES[index]));
            }
        }
        return result;
    }

    bool EditorShortcuts::pressed(
        const Action action, const ImGuiInputFlags flags) const {
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
