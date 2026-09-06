#pragma once

#include "command_history.h"

namespace CometEditor::SceneCommands {
    [[nodiscard]] Comet::EntityUuid create_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, std::string name = "Entity");
    [[nodiscard]] bool delete_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity);
    [[nodiscard]] Comet::EntityUuid duplicate_entity(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity);
    [[nodiscard]] bool reparent_entity(
        CommandHistory& history, Comet::EntityUuid entity, Comet::EntityUuid parent = {});
    [[nodiscard]] bool can_edit_component_structure(
        const Comet::ComponentDescriptor& component);
    [[nodiscard]] bool add_component(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity,
        std::string_view component);
    [[nodiscard]] bool remove_component(CommandHistory& history,
        const Comet::ComponentRegistry& registry, Comet::EntityUuid entity,
        std::string_view component);
}
