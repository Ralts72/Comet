#pragma once
#include "editor_panel.h"
#include "scene/entity_uuid.h"

#include <cstdint>
#include <optional>

namespace Comet {
    class Entity;
    class Scene;
}

namespace CometEditor {
    class SelectionService;
    class CommandHistory;
    struct EditorState;

    class HierarchyPanel: public EditorPanel {
    public:
        struct Request {
            enum class Type { Create, Delete, Reparent };
            Type type;
            Comet::EntityUuid entity;
            Comet::EntityUuid parent;
            std::uint64_t generation;
        };

        HierarchyPanel(Comet::Scene& scene, SelectionService& selection,
            const CommandHistory& history, const EditorState& state);

        void render() override;

        void set_scene(Comet::Scene& scene);
        [[nodiscard]] std::optional<Request> take_request();

    private:
        [[nodiscard]] bool can_edit_scene() const;
        void render_entity_node(Comet::Entity entity);

        void accept_reparent_drop(Comet::Entity parent);

        Comet::Scene* m_scene;
        SelectionService& m_selection;
        const CommandHistory& m_history;
        const EditorState& m_state;
        std::optional<Request> m_request;
    };

}
