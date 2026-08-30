#pragma once

#include "scene/component_registry.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CometEditor {
    class EditorCommand {
    public:
        virtual ~EditorCommand() = default;

        [[nodiscard]] virtual bool undo() = 0;
        [[nodiscard]] virtual bool redo() = 0;
    };

    class EditorCommandHistory {
    public:
        explicit EditorCommandHistory(std::size_t capacity = 256);

        [[nodiscard]] bool execute(std::unique_ptr<EditorCommand> command);
        [[nodiscard]] bool push_applied(
            std::unique_ptr<EditorCommand> command);
        [[nodiscard]] bool undo();
        [[nodiscard]] bool redo();

        void clear();

        [[nodiscard]] bool can_undo() const { return !m_undo.empty(); }
        [[nodiscard]] bool can_redo() const { return !m_redo.empty(); }
        [[nodiscard]] std::size_t undo_size() const { return m_undo.size(); }
        [[nodiscard]] std::size_t redo_size() const { return m_redo.size(); }

    private:
        void push_undo(std::unique_ptr<EditorCommand> command);

        std::size_t m_capacity;
        std::vector<std::unique_ptr<EditorCommand>> m_undo;
        std::vector<std::unique_ptr<EditorCommand>> m_redo;
    };

    class EntityPropertyEditCommand final : public EditorCommand {
    public:
        using SceneGetter = std::function<Comet::Scene*()>;

        EntityPropertyEditCommand(
            SceneGetter get_scene,
            const Comet::ComponentRegistry& component_registry,
            Comet::EntityUuid entity_uuid,
            std::string component_id,
            std::string property_id,
            Comet::PropertyValue before,
            Comet::PropertyValue after);

        [[nodiscard]] bool undo() override;
        [[nodiscard]] bool redo() override;

    private:
        [[nodiscard]] bool apply(const Comet::PropertyValue& value) const;

        SceneGetter m_get_scene;
        const Comet::ComponentRegistry& m_component_registry;
        Comet::EntityUuid m_entity_uuid;
        std::string m_component_id;
        std::string m_property_id;
        Comet::PropertyValue m_before;
        Comet::PropertyValue m_after;
    };
}
