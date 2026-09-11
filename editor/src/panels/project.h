#pragma once
#include "asset/database.h"
#include "editor_panel.h"

#include <array>
#include <map>
#include <filesystem>
#include <functional>
#include <optional>
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
        struct MoveRequest {
            Comet::AssetHandle handle;
            Comet::AssetRevision revision;
            std::filesystem::path destination;
        };
        [[nodiscard]] static AssetTreeNode build_asset_tree(
            std::vector<Comet::AssetRecord> assets);
        void render_asset_tree(
            const AssetTreeNode& node, const std::filesystem::path& path);
        void accept_asset_drop(const std::filesystem::path& directory);
        void request_rename(const Comet::AssetRecord& record);
        void render_rename_dialog();
        bool move_asset(
            Comet::AssetHandle handle, const std::filesystem::path& destination);

        const Comet::AssetDatabase& m_database;
        AssetTreeNode m_tree;
        Comet::AssetScanReport m_scan_report;
        RefreshCallback m_refresh_callback;
        MoveAssetCallback m_move_asset_callback;
        SelectionService& m_selection;
        std::array<char, 1024> m_name_buffer{};
        std::string m_operation_error;
        Comet::AssetHandle m_renaming_asset;
        bool m_rename_requested = false;
        bool m_refresh_requested = false;
        std::optional<MoveRequest> m_pending_move;
    };
}
