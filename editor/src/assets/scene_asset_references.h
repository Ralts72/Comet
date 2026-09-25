#pragma once

#include "asset/asset_manager.h"
#include "asset/database.h"
#include "asset/reference.h"

#include <set>
#include <span>
#include <unordered_set>

namespace Comet {
    class ComponentRegistry;
    class Scene;
}

namespace CometEditor {
    class SceneAssetReferences final {
    public:
        SceneAssetReferences(Comet::AssetDatabase& database, Comet::AssetManager& manager);

        [[nodiscard]] Comet::Result<std::size_t, Comet::Error> prepare_scene(
            Comet::Scene& scene, const Comet::ComponentRegistry& components);
        void track_scene(Comet::Scene& scene, const Comet::ComponentRegistry& components);
        void accept_scan(const Comet::AssetScanReport& report);
        void mark_changed(Comet::AssetHandle handle);
        void mark_changed(std::span<const Comet::AssetHandle> handles);
        [[nodiscard]] Comet::Result<std::size_t, Comet::Error> restore(
            Comet::AssetCompletionBudget budget = {});

    private:
        Comet::AssetDatabase& m_database;
        Comet::AssetManager& m_manager;
        std::unordered_set<Comet::AssetHandle> m_changes;
        std::set<Comet::AssetReference> m_scene_references;
        std::set<Comet::AssetReference> m_pending_references;
        std::set<Comet::AssetReference> m_unresolved_references;
    };
}
