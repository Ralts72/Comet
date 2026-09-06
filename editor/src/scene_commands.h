#pragma once

#include "command_history.h"

namespace CometEditor::SceneCommands {
    [[nodiscard]] bool can_edit_component_structure(
        const Comet::ComponentDescriptor& component);
    [[nodiscard]] bool add_component(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity,
        std::string_view component);
    [[nodiscard]] bool remove_component(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity,
        std::string_view component);
}
