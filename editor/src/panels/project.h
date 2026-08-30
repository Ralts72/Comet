#pragma once
#include "asset/database.h"
#include "editor.h"

#include <functional>
#include <vector>

namespace CometEditor {
    class SelectionService;

    class ProjectPanel : public EditorPanel {
    public:
        using RefreshCallback = std::function<Comet::AssetScanReport()>;

        ProjectPanel(
            const Comet::AssetDatabase& database,
            Comet::AssetScanReport scan_report,
            RefreshCallback refresh_callback,
            SelectionService& selection);

        void render() override;
        void update_scan_report(Comet::AssetScanReport scan_report);

    private:
        const Comet::AssetDatabase& m_database;
        std::vector<Comet::AssetRecord> m_assets;
        Comet::AssetScanReport m_scan_report;
        RefreshCallback m_refresh_callback;
        SelectionService& m_selection;
        int m_view_mode = 0; // 0: Assets, 1: Packages
    };
}
