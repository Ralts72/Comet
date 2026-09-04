#pragma once
#include "asset/database.h"
#include "editor.h"

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace CometEditor {
    class SelectionService;

    class ProjectPanel: public EditorPanel {
    public:
        using RefreshCallback = std::function<Comet::AssetScanReport()>;
        using MoveAssetCallback = std::function<Comet::AssetScanReport(
            Comet::AssetHandle, const std::filesystem::path&)>;

        ProjectPanel(const Comet::AssetDatabase& database,
            Comet::AssetScanReport scan_report, RefreshCallback refresh_callback,
            MoveAssetCallback move_asset_callback, SelectionService& selection);

        void render() override;
        void update_scan_report(Comet::AssetScanReport scan_report);

    private:
        void request_asset_move(const Comet::AssetRecord& record);
        void render_asset_move_dialog();

        const Comet::AssetDatabase& m_database;
        std::vector<Comet::AssetRecord> m_assets;
        Comet::AssetScanReport m_scan_report;
        RefreshCallback m_refresh_callback;
        MoveAssetCallback m_move_asset_callback;
        SelectionService& m_selection;
        std::array<char, 1024> m_move_path_buffer{};
        std::string m_move_error;
        Comet::AssetHandle m_moving_asset;
        bool m_move_dialog_open_requested = false;
        int m_view_mode = 0; // 0: Assets, 1: Packages
    };
}
