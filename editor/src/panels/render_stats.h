#pragma once

#include "panels/editor_panel.h"
#include <optional>

namespace Comet {
    class Engine;
}

namespace CometEditor {
    class RenderStatsPanel final: public EditorPanel {
    public:
        explicit RenderStatsPanel(const Comet::Engine& engine);
        void render() override;
        [[nodiscard]] std::optional<bool> take_capture_request();
        [[nodiscard]] bool take_allocation_report_request();

    private:
        const Comet::Engine& m_engine;
        std::optional<bool> m_capture_request;
        bool m_allocation_report_request = false;
    };
}
