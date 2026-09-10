#pragma once

#include "asset/asset_manager.h"
#include "asset/source_monitor.h"

namespace CometEditor {
    // 编辑器拥有文件监视与资源准备策略；不持有面板或 GPU 对象。
    class EditorAssets {
    public:
        EditorAssets(Comet::ProjectPaths paths, Comet::AssetRegistry& registry,
            Comet::RenderResourceFactory& factory, Comet::TaskScheduler& scheduler);

        [[nodiscard]] Comet::AssetScanReport refresh();
        [[nodiscard]] std::optional<Comet::AssetScanReport> update();
        [[nodiscard]] Comet::AssetScanReport move(
            Comet::AssetHandle handle, const std::filesystem::path& destination);
        [[nodiscard]] bool update_material(
            Comet::AssetHandle handle, const Comet::MaterialData& data);
        [[nodiscard]] bool reimport_texture(
            Comet::AssetHandle handle, Comet::TextureImportSettings settings);
        [[nodiscard]] bool prepare_reference(
            Comet::AssetHandle handle, Comet::AssetType type);
        [[nodiscard]] const Comet::AssetDatabase& database() const {
            return m_manager.get_database();
        }

    private:
        void observe(const Comet::AssetSourceMonitor::PollResult& result);
        void accept_scan(const Comet::AssetScanReport& report);
        void acknowledge(const std::filesystem::path& path);

        Comet::AssetManager m_manager;
        Comet::AssetSourceMonitor m_monitor;
        std::string m_monitor_error;
    };
}
