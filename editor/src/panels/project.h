#pragma once
#include "asset/database.h"
#include "editor_panel.h"

#include <array>
#include <map>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace CometEditor {
    class SelectionService;

    class ProjectPanel: public EditorPanel {
    public:
        using RefreshCallback = std::function<void()>;
        using MoveAssetCallback = std::function<Comet::AssetScanReport(
            Comet::AssetHandle, const std::filesystem::path&)>;

        ProjectPanel(const Comet::AssetDatabase& database,
            Comet::AssetScanReport scan_report, RefreshCallback refresh_callback,
            MoveAssetCallback move_asset_callback, SelectionService& selection);

        void render() override;
        void update_scan_report(Comet::AssetScanReport scan_report);

    private:
        struct AssetTreeNode {
            std::map<std::string, AssetTreeNode> directories;
            std::vector<Comet::AssetRecord> assets;
        };
        [[nodiscard]] static AssetTreeNode build_asset_tree(
            std::vector<Comet::AssetRecord> assets);
        void render_asset_tree(const AssetTreeNode& node);
        void request_asset_move(const Comet::AssetRecord& record);
        void render_asset_move_dialog();

        const Comet::AssetDatabase& m_database;
        AssetTreeNode m_tree;
        Comet::AssetScanReport m_scan_report;
        RefreshCallback m_refresh_callback;
        MoveAssetCallback m_move_asset_callback;
        SelectionService& m_selection;
        std::array<char, 1024> m_move_path_buffer{};
        std::string m_move_error;
        Comet::AssetHandle m_moving_asset;
        bool m_move_dialog_open_requested = false;
        int m_view_mode = 0; // 0：资产，1：包
    };
}
