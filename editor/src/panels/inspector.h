#pragma once
#include "editor.h"

namespace CometEditor {
    class SelectionService;

    class InspectorPanel : public EditorPanel {
    public:
        explicit InspectorPanel(SelectionService& selection);

        void render() override;

    private:
        SelectionService& m_selection;
    };

}
