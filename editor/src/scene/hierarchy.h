#pragma once
#include "ui/editor_panel.h"
#include "scene/entity_uuid.h"
#include "scene/scene_editor.h"

#include <cstdint>
#include <optional>
#include <string>

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
        using Request = SceneEditor::StructureRequest;
        struct RenameRequest {
            Comet::EntityUuid entity;
            std::string name;
            std::uint64_t generation;
        };

        HierarchyPanel(Comet::Scene& scene, SelectionService& selection,
            const CommandHistory& history, const EditorState& state);

        void render() override;

        void set_scene(Comet::Scene& scene);
        [[nodiscard]] std::optional<Request> take_request();
        [[nodiscard]] std::optional<RenameRequest> take_rename_request();

    private:
        [[nodiscard]] bool can_edit_scene() const;
        void render_entity_node(Comet::Entity entity);
        void render_context_menu(Comet::Entity entity);
        void render_rename_dialog();

        void accept_reparent_drop(Comet::Entity parent);

        Comet::Scene* m_scene;
        SelectionService& m_selection;
        const CommandHistory& m_history;
        const EditorState& m_state;
        std::optional<Request> m_request;
        std::optional<RenameRequest> m_rename_request;
        Comet::EntityUuid m_renaming_entity;
        std::uint64_t m_rename_generation = 0;
        std::string m_rename_name;
        bool m_open_rename = false;
        Comet::EntityUuid m_expand_entity;
    };

}
