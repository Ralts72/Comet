#pragma once
#include "editor.h"

namespace Comet {
    class Entity;
    class Scene;
}

namespace CometEditor {
    class SelectionService;

    class HierarchyPanel : public EditorPanel {
    public:
        HierarchyPanel(Comet::Scene& scene, SelectionService& selection);

        void render() override;

    private:
        void render_entity_node(Comet::Entity entity);

        void accept_reparent_drop(Comet::Entity parent);

        Comet::Scene& m_scene;
        SelectionService& m_selection;
    };

}
