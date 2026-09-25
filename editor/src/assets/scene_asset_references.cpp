#include "assets/scene_asset_references.h"

#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "graphics/result.h"
#include "scene/component_registry.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace CometEditor {
    SceneAssetReferences::SceneAssetReferences(
        Comet::AssetDatabase& database, Comet::AssetManager& manager)
        : m_database(database), m_manager(manager) {}

    Comet::Result<std::size_t, Comet::Error> SceneAssetReferences::prepare_scene(
        Comet::Scene& scene, const Comet::ComponentRegistry& components) {
        return m_manager.prepare_references(components.collect_asset_references(scene),
            Comet::AssetManager::MissingAssetPolicy::AllowMissing);
    }

    void SceneAssetReferences::track_scene(
        Comet::Scene& scene, const Comet::ComponentRegistry& components) {
        const auto references = components.collect_asset_references(scene);
        std::set<Comet::AssetReference> next(references.begin(), references.end());
        for(const auto& reference : next)
            if(!m_scene_references.contains(reference))
                m_pending_references.insert(reference);
        m_scene_references = std::move(next);
        std::erase_if(m_pending_references,
            [&](const auto& reference) { return !m_scene_references.contains(reference); });
        std::erase_if(m_unresolved_references,
            [&](const auto& reference) { return !m_scene_references.contains(reference); });
    }

    void SceneAssetReferences::accept_scan(const Comet::AssetScanReport& report) {
        if(!report.snapshot_updated)
            return;
        mark_changed(report.added_assets);
        mark_changed(report.modified_assets);
        mark_changed(report.removed_assets);
        m_pending_references.insert(m_unresolved_references.begin(), m_unresolved_references.end());
    }

    void SceneAssetReferences::mark_changed(const Comet::AssetHandle handle) {
        m_changes.insert(handle);
    }

    void SceneAssetReferences::mark_changed(const std::span<const Comet::AssetHandle> handles) {
        m_changes.insert(handles.begin(), handles.end());
    }

    Comet::Result<std::size_t, Comet::Error> SceneAssetReferences::restore(
        const Comet::AssetCompletionBudget budget) {
        PROFILE_SCOPE("SceneAssetReferences::restore");
        auto changed = std::exchange(m_changes, {});
        if(!changed.empty()) {
            m_database.include_dependents(changed);
            for(const auto& reference : m_scene_references)
                if(changed.contains(reference.handle))
                    m_pending_references.insert(reference);
            // 失败的材质可能尚无完整依赖索引，发布事件也重试有限的未解析引用。
            m_pending_references.insert(
                m_unresolved_references.begin(), m_unresolved_references.end());
        }
        const auto start = std::chrono::steady_clock::now();
        std::size_t processed = 0;
        while(!m_pending_references.empty() && processed < budget.max_results
              && budget.max_time > std::chrono::nanoseconds::zero()
              && (processed == 0 || std::chrono::steady_clock::now() - start < budget.max_time)) {
            const auto reference = *m_pending_references.begin();
            m_pending_references.erase(m_pending_references.begin());
            ++processed;
            auto loaded = m_manager.request_load(reference.handle, reference.type);
            if(loaded) {
                m_unresolved_references.erase(reference);
            } else {
                m_unresolved_references.insert(reference);
                if(Comet::is_device_lost(loaded.error()))
                    return Comet::Result<std::size_t, Comet::Error>::failure(loaded.error());
                LOG_WARN(
                    "Unresolved asset {}: {}", reference.handle.value(), loaded.error().message);
            }
        }
        return Comet::Result<std::size_t, Comet::Error>::success(processed);
    }
}
