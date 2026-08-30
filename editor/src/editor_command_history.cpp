#include "editor_command_history.h"

#include "scene/scene.h"

#include <algorithm>
#include <utility>

namespace CometEditor {
    EditorCommandHistory::EditorCommandHistory(const std::size_t capacity)
        : m_capacity(std::max<std::size_t>(capacity, 1)) {}

    bool EditorCommandHistory::execute(
        std::unique_ptr<EditorCommand> command) {
        if(!command || !command->redo()) {
            return false;
        }
        m_redo.clear();
        push_undo(std::move(command));
        return true;
    }

    bool EditorCommandHistory::push_applied(
        std::unique_ptr<EditorCommand> command) {
        if(!command) {
            return false;
        }
        m_redo.clear();
        push_undo(std::move(command));
        return true;
    }

    bool EditorCommandHistory::undo() {
        if(m_undo.empty() || !m_undo.back()->undo()) {
            return false;
        }
        m_redo.push_back(std::move(m_undo.back()));
        m_undo.pop_back();
        return true;
    }

    bool EditorCommandHistory::redo() {
        if(m_redo.empty() || !m_redo.back()->redo()) {
            return false;
        }
        push_undo(std::move(m_redo.back()));
        m_redo.pop_back();
        return true;
    }

    void EditorCommandHistory::clear() {
        m_undo.clear();
        m_redo.clear();
    }

    void EditorCommandHistory::push_undo(
        std::unique_ptr<EditorCommand> command) {
        if(m_undo.size() == m_capacity) {
            m_undo.erase(m_undo.begin());
        }
        m_undo.push_back(std::move(command));
    }

    EntityPropertyEditCommand::EntityPropertyEditCommand(
        SceneGetter get_scene,
        const Comet::ComponentRegistry& component_registry,
        const Comet::EntityUuid entity_uuid,
        std::string component_id,
        std::string property_id,
        Comet::PropertyValue before,
        Comet::PropertyValue after)
        : m_get_scene(std::move(get_scene)),
          m_component_registry(component_registry),
          m_entity_uuid(entity_uuid),
          m_component_id(std::move(component_id)),
          m_property_id(std::move(property_id)),
          m_before(std::move(before)),
          m_after(std::move(after)) {}

    bool EntityPropertyEditCommand::undo() {
        return apply(m_before);
    }

    bool EntityPropertyEditCommand::redo() {
        return apply(m_after);
    }

    bool EntityPropertyEditCommand::apply(
        const Comet::PropertyValue& value) const {
        Comet::Scene* scene = m_get_scene ? m_get_scene() : nullptr;
        if(scene == nullptr || !m_entity_uuid) {
            return false;
        }
        Comet::Entity entity = scene->find_entity(m_entity_uuid);
        const Comet::ComponentDescriptor* component_descriptor =
            m_component_registry.find_component(m_component_id);
        if(!entity || component_descriptor == nullptr) {
            return false;
        }
        void* component = component_descriptor->get_component(entity);
        const Comet::PropertyDescriptor* property_descriptor =
            component_descriptor->find_property(m_property_id);
        return component != nullptr
            && property_descriptor != nullptr
            && property_descriptor->assign_value(component, value);
    }
}
