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
        struct DeleteRequest {
            Comet::AssetHandle handle;
            Comet::AssetRevision revision;
        };
        struct CreateMaterialRequest {
            std::filesystem::path destination;
            Comet::MaterialData data;
        };
        struct CreateScriptRequest {
            std::filesystem::path destination;
        };

        ProjectPanel(const Comet::AssetDatabase& database, std::filesystem::path asset_root,
            Comet::AssetScanReport scan_report, SelectionService& selection,
            const CommandHistory& history);

        void render() override;
        void update_scan_report(Comet::AssetScanReport scan_report);
        [[nodiscard]] bool take_refresh_request();
        [[nodiscard]] std::optional<MoveRequest> take_move_request();
        void complete_move(const MoveRequest& request, Comet::AssetScanReport report);
        [[nodiscard]] std::optional<DeleteRequest> take_delete_request();
        void complete_delete(const DeleteRequest& request, Comet::AssetScanReport report);
        void set_material_layouts(
            std::vector<std::shared_ptr<const Comet::MaterialLayout>> layouts);
        [[nodiscard]] std::optional<CreateMaterialRequest> take_create_material_request();
        void complete_create_material(
            const CreateMaterialRequest& request, Comet::AssetScanReport report);
        [[nodiscard]] std::optional<CreateScriptRequest> take_create_script_request();
        void complete_create_script(
            const CreateScriptRequest& request, Comet::AssetScanReport report);
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
        void request_delete(const Comet::AssetRecord& record);
        void render_delete_dialog();
        void render_directory_menu(const std::filesystem::path& directory);
        void request_create_material(const std::filesystem::path& directory);
        void request_create_script(const std::filesystem::path& directory);
        void render_create_material_dialog();
        void render_create_script_dialog();
        void complete_create_asset(
            const std::filesystem::path& destination, Comet::AssetScanReport report, bool script);

        const Comet::AssetDatabase& m_database;
        std::filesystem::path m_asset_root;
        AssetTreeNode m_tree;
        std::vector<DropTarget> m_drop_targets;
        Comet::AssetScanReport m_scan_report;
        SelectionService& m_selection;
        const CommandHistory& m_history;
        std::optional<Comet::AssetHandle> m_reimport_request;
        std::string m_rename_name;
        std::string m_operation_error;
        Comet::AssetHandle m_renaming_asset;
        bool m_rename_requested = false;
        bool m_close_rename = false;
        bool m_refresh_requested = false;
        std::optional<MoveRequest> m_pending_move;
        Comet::AssetHandle m_deleting_asset;
        bool m_delete_requested = false;
        bool m_close_delete = false;
        std::optional<DeleteRequest> m_pending_delete;
        std::vector<std::shared_ptr<const Comet::MaterialLayout>> m_material_layouts;
        std::filesystem::path m_create_directory;
        std::array<char, 256> m_material_name{};
        std::string m_create_template;
        bool m_create_requested = false;
        bool m_close_create = false;
        std::optional<CreateMaterialRequest> m_pending_create;
        std::array<char, 256> m_script_name{};
        bool m_create_script_requested = false;
        bool m_close_create_script = false;
        std::optional<CreateScriptRequest> m_pending_script_create;
    };
}
