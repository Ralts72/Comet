#pragma once
#include "editor_panel.h"

namespace CometEditor {

    class InspectorPanel : public EditorPanel {
    public:
        InspectorPanel();

        void render() override;

        void set_selected_object(const std::string& object_name) { m_selected_object = object_name; }

        [[nodiscard]] const std::string& get_selected_object() const { return m_selected_object; }

    private:
        std::string m_selected_object;

        float m_position[3] = {0.0f, 0.0f, 0.0f};
        float m_rotation[3] = {0.0f, 0.0f, 0.0f};
        float m_scale[3] = {1.0f, 1.0f, 1.0f};
    };

}

