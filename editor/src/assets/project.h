#pragma once
#include "asset/database.h"
#include "ui/editor_panel.h"
#include "core/math_utils.h"

#include <array>
#include <map>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace CometEditor {
    class SelectionService;
    class CommandHistory;

    class ProjectPanel: public EditorPanel {
    public:
        using RefreshCallback = std::function<Comet::AssetScanReport()>;
        using MoveAssetCallback = std::function<Comet::AssetScanReport(
            Comet::AssetHandle, const std::filesystem::path&)>;

        ProjectPanel(const Comet::AssetDatabase& database,
            std::filesystem::path asset_root, Comet::AssetScanReport scan_report,
            RefreshCallback refresh_callback, MoveAssetCallback move_asset_callback,
            SelectionService& selection, const CommandHistory& history);

        void render() override;
        void update_scan_report(Comet::AssetScanReport scan_report);
        [[nodiscard]] std::optional<Comet::AssetHandle> take_mesh_reimport_request();
        [[nodiscard]] std::optional<std::filesystem::path> file_drop_directory(
            Comet::Math::Vec2 position) const;

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
        struct DropTarget {
            Comet::Math::Vec2 minimum;
            Comet::Math::Vec2 maximum;
            std::filesystem::path directory;
        };
        [[nodiscard]] AssetTreeNode build_asset_tree() const;
        void record_drop_target(const std::filesystem::path& directory);
        void render_asset_tree(
            const AssetTreeNode& node, const std::filesystem::path& path);
        void accept_asset_drop(const std::filesystem::path& directory);
        void request_rename(const Comet::AssetRecord& record);
        void render_rename_dialog();
        bool move_asset(
            Comet::AssetHandle handle, const std::filesystem::path& destination);

        const Comet::AssetDatabase& m_database;
        std::filesystem::path m_asset_root;
        AssetTreeNode m_tree;
        std::vector<DropTarget> m_drop_targets;
        Comet::AssetScanReport m_scan_report;
        RefreshCallback m_refresh_callback;
        MoveAssetCallback m_move_asset_callback;
        SelectionService& m_selection;
        const CommandHistory& m_history;
        std::optional<Comet::AssetHandle> m_reimport_request;
        std::array<char, 1024> m_name_buffer{};
        std::string m_operation_error;
        Comet::AssetHandle m_renaming_asset;
        bool m_rename_requested = false;
        bool m_refresh_requested = false;
        std::optional<MoveRequest> m_pending_move;
    };
}
