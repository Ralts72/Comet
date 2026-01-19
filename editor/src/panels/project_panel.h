#pragma once
#include "editor_panel.h"

namespace CometEditor {

    class ProjectPanel : public EditorPanel {
    public:
        ProjectPanel();
        
        void render() override;

    private:
        int m_view_mode = 0;  // 0: Assets, 1: Packages
    };
}