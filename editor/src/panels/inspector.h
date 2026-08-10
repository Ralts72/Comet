#pragma once
#include "editor.h"

namespace Comet {
    class ComponentRegistry;
}

namespace CometEditor {
    class PropertyEditorRegistry;
    class SelectionService;

    class InspectorPanel : public EditorPanel {
    public:
        InspectorPanel(
            SelectionService& selection,
            const Comet::ComponentRegistry& component_registry,
            const PropertyEditorRegistry& property_editor_registry);

        void render() override;

    private:
        SelectionService& m_selection;
        const Comet::ComponentRegistry& m_component_registry;
        const PropertyEditorRegistry& m_property_editor_registry;
    };

}
