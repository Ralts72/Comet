#pragma once

#include "common/result.h"
#include "input/input_actions.h"
#include "ui/editor_panel.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CometEditor {
    class InputSettingsPanel final: public EditorPanel {
    public:
        InputSettingsPanel();
        void request(const Comet::InputActions& current);
        void render() override;
        [[nodiscard]] std::optional<Comet::InputActions> take_request();
        void complete(const Comet::Result<void>& result);

    private:
        struct BindingDraft {
            std::string source;
            std::string control;
            float scale = 1;
            float deadzone = 0;
        };
        struct ActionDraft {
            std::string name;
            Comet::InputActions::Type type = Comet::InputActions::Type::Button;
            std::vector<BindingDraft> bindings;
        };

        [[nodiscard]] Comet::Result<Comet::InputActions> build() const;
        void render_action(std::size_t index);
        void render_binding(std::size_t action_index, std::size_t binding_index);
        void capture_key();

        std::vector<ActionDraft> m_actions;
        std::optional<std::size_t> m_selected_action;
        std::optional<std::pair<std::size_t, std::size_t>> m_capturing;
        std::optional<Comet::InputActions> m_request;
        std::string m_error;
    };
}
