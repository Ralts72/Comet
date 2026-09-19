#pragma once
#include "asset/database.h"
#include "ui/editor_panel.h"
#include "core/math_utils.h"
#include "assets/material_editing.h"

#include <array>
#include <map>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace CometEditor {
    class SelectionService;
    class CommandHistory;

    class ProjectPanel: public EditorPanel {
    public:
        struct MoveRequest {
            Comet::AssetHandle handle;
            Comet::AssetRevision revision;
            std::filesystem::path destination;
        };
        struct CreateMaterialRequest {
            std::filesystem::path destination;
            Comet::MaterialData data;
        };

        ProjectPanel(const Comet::AssetDatabase& database, std::filesystem::path asset_root,
            Comet::AssetScanReport scan_report, SelectionService& selection,
            const CommandHistory& history);

        void render() override;
        void update_scan_report(Comet::AssetScanReport scan_report);
        [[nodiscard]] bool take_refresh_request();
        [[nodiscard]] std::optional<MoveRequest> take_move_request();
        void complete_move(const MoveRequest& request, Comet::AssetScanReport report);
        void set_material_layouts(
            std::vector<std::shared_ptr<const Comet::MaterialLayout>> layouts);
        [[nodiscard]] std::optional<CreateMaterialRequest> take_create_material_request();
        void complete_create_material(
            const CreateMaterialRequest& request, Comet::AssetScanReport report);
        [[nodiscard]] std::optional<Comet::AssetHandle> take_mesh_reimport_request();
        [[nodiscard]] std::optional<std::filesystem::path> file_drop_directory(
            Comet::Math::Vec2 position) const;

    private:
        struct AssetTreeNode {
            std::map<std::string, AssetTreeNode> directories;
            std::vector<Comet::AssetRecord> assets;
        };
        struct DropTarget {
            Comet::Math::Vec2 minimum;
            Comet::Math::Vec2 maximum;
            std::filesystem::path directory;
        };
        [[nodiscard]] AssetTreeNode build_asset_tree() const;
        void record_drop_target(const std::filesystem::path& directory);
        void render_asset_tree(const AssetTreeNode& node, const std::filesystem::path& path);
        void accept_asset_drop(const std::filesystem::path& directory);
        void request_rename(const Comet::AssetRecord& record);
        void render_rename_dialog();
        void render_directory_menu(const std::filesystem::path& directory);
        void request_create_material(const std::filesystem::path& directory);
        void render_create_material_dialog();

        const Comet::AssetDatabase& m_database;
        std::filesystem::path m_asset_root;
        AssetTreeNode m_tree;
        std::vector<DropTarget> m_drop_targets;
        Comet::AssetScanReport m_scan_report;
        SelectionService& m_selection;
        const CommandHistory& m_history;
        std::optional<Comet::AssetHandle> m_reimport_request;
        std::array<char, 1024> m_name_buffer{};
        std::string m_operation_error;
        Comet::AssetHandle m_renaming_asset;
        bool m_rename_requested = false;
        bool m_close_rename = false;
        bool m_refresh_requested = false;
        std::optional<MoveRequest> m_pending_move;
        std::vector<std::shared_ptr<const Comet::MaterialLayout>> m_material_layouts;
        std::filesystem::path m_create_directory;
        std::array<char, 256> m_material_name{};
        std::string m_create_template;
        bool m_create_requested = false;
        bool m_close_create = false;
        std::optional<CreateMaterialRequest> m_pending_create;
    };
}
