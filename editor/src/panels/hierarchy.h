#pragma once
#include "editor.h"

namespace Comet {
    class Scene;
}

namespace CometEditor {
    class SelectionService;

    class HierarchyPanel : public EditorPanel {
    public:
        HierarchyPanel(Comet::Scene& scene, SelectionService& selection);

        void render() override;

    private:
        Comet::Scene& m_scene;
        SelectionService& m_selection;
    };

}
