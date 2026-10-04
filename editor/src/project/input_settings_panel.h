#pragma once

#include "common/result.h"
#include "input/input_actions.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CometEditor {
    class InputSettingsPanel final {
    public:
        void request(const Comet::InputActions& current);
        [[nodiscard]] bool is_open() const { return m_open; }
        void close();
        void render(const Comet::Input::Frame& input);
        [[nodiscard]] std::optional<Comet::InputActions> take_request();
        void complete(const Comet::Result<void>& result);

    private:
        struct BindingDraft {
            std::string source;
            std::string control;
            float scale = 1;
            float deadzone = 0;
            Comet::Uuid id = Comet::Uuid::generate();
        };
        struct ActionDraft {
            std::string name;
            Comet::InputActions::Type type = Comet::InputActions::Type::Button;
            std::vector<BindingDraft> bindings;
            std::optional<std::size_t> context;
            Comet::Uuid id = Comet::Uuid::generate();
        };
        struct Capture {
            Comet::Uuid action;
            Comet::Uuid binding;
            std::uint64_t serial;
            std::uint64_t interruption;
            std::uint32_t owner;
        };

        [[nodiscard]] Comet::Result<Comet::InputActions> build() const;
        void render_contexts();
        void render_action(std::size_t index, const Comet::Input::Frame& input);
        void render_binding(
            std::size_t action_index, std::size_t binding_index, const Comet::Input::Frame& input);
        void render_binding_relationships(std::size_t action_index);
        void capture_key(const Comet::Input::Frame& input);
        void cancel_capture();

        std::vector<ActionDraft> m_actions;
        std::vector<Comet::InputActions::Context> m_contexts;
        std::optional<std::size_t> m_selected_action;
        std::optional<Capture> m_capturing;
        std::optional<Comet::InputActions> m_request;
        std::string m_error;
        bool m_open = false;
    };
}
