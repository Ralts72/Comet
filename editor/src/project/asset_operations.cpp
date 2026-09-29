#include "project/asset_operations.h"

#include "assets/editor_assets.h"
#include "core/project.h"
#include "diagnostics/logger.h"
#include "project/project_session.h"
#include "scene/scene_document.h"

#include <string>
#include <utility>

namespace CometEditor {
    namespace {
        Comet::AssetScanReport rejected(
            const EditorAssets& assets, const std::filesystem::path& path, std::string message) {
            Comet::AssetScanReport report;
            report.indexed_assets = assets.database().size();
            report.issues.push_back({path, std::move(message)});
            return report;
        }

        Comet::AssetScanReport rollback_move(EditorAssets& assets, SceneDocument& document,
            const Comet::AssetHandle handle, const std::filesystem::path& source,
            std::string error) {
            auto rollback = assets.move(handle, source);
            if(rollback.snapshot_updated && rollback.succeeded()) {
                // 业务操作失败，但回滚扫描仍发布了真实索引（可能包含其他新资产）。
                rollback.issues.push_back({source, std::move(error)});
                return rollback;
            }

            // 补偿失败时重新对照磁盘，文档不能继续向一个已经移走的路径保存。
            auto report = assets.refresh();
            if(const auto* current = assets.database().find(handle))
                document.relocate_asset(source, current->path);
            report.issues.push_back({source, std::move(error) + "; asset move rollback failed"});
            report.issues.insert(
                report.issues.end(), rollback.issues.begin(), rollback.issues.end());
            return report;
        }
    }

    Comet::AssetScanReport move_project_asset(EditorAssets& assets, Comet::Project& project,
        SceneDocument& document, ProjectSession& session, const Comet::AssetHandle handle,
        const std::filesystem::path& destination) {
        const auto* record = assets.database().find(handle);
        if(!record || record->type != Comet::AssetType::Scene)
            return assets.move(handle, destination);
        const auto source = record->path;
        auto report = assets.move(handle, destination);
        if(!report.snapshot_updated || !report.succeeded())
            return report;
        const auto moved = assets.database().find(handle)->path;
        const bool startup_changed = project.startup_scene() == source;
        if(startup_changed) {
            if(auto saved = project.save_startup_scene(moved); !saved)
                return rollback_move(assets, document, handle, source,
                    "Cannot update startup scene: " + saved.error());
        }
        if(session.last_scene() == source) {
            if(auto saved = session.record_scene(moved); !saved)
                LOG_WARN("Asset moved, but cannot save editor session: {}", saved.error());
        }
        document.relocate_asset(source, moved);
        return report;
    }

    Comet::AssetScanReport remove_project_asset(EditorAssets& assets, const Comet::Project& project,
        const SceneDocument& document, const Comet::AssetHandle handle) {
        if(const auto* record = assets.database().find(handle);
            record && record->type == Comet::AssetType::Scene) {
            if(record->path == project.startup_scene())
                return rejected(assets, record->path,
                    "Choose another startup scene before deleting this scene");
            if(record->path == document.get_asset_relative_path())
                return rejected(
                    assets, record->path, "Close the current scene before deleting its asset");
        }
        return assets.remove(handle);
    }
}
