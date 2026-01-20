#pragma once
#include "editor_panel.h"

namespace CometEditor {

    class HierarchyPanel : public EditorPanel {
    public:
        HierarchyPanel();
        
        void render() override;
        
        using SelectionCallback = std::function<void(const std::string&)>;
        void set_selection_callback(const SelectionCallback& callback) {
            m_selection_callback = callback;
        }
        
        [[nodiscard]] const std::string& get_selected_object() const {
            return m_selected_object;
        }

    private:
        std::string m_selected_object;
        SelectionCallback m_selection_callback;
    };

}

