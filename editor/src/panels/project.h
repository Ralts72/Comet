#pragma once
#include "asset/database.h"
#include "editor.h"

#include <functional>
#include <vector>

namespace CometEditor {

    class ProjectPanel : public EditorPanel {
    public:
        using RefreshCallback = std::function<Comet::AssetScanReport()>;

        ProjectPanel(
            const Comet::AssetDatabase& database,
            Comet::AssetScanReport scan_report,
            RefreshCallback refresh_callback);

        void render() override;

    private:
        const Comet::AssetDatabase& m_database;
        std::vector<Comet::AssetRecord> m_assets;
        Comet::AssetScanReport m_scan_report;
        RefreshCallback m_refresh_callback;
        Comet::AssetHandle m_selected_asset;
        int m_view_mode = 0; // 0: Assets, 1: Packages
    };
}
