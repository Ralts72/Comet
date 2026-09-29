#pragma once

#include "asset/database.h"

namespace Comet {
    class Project;
}

namespace CometEditor {
    class EditorAssets;
    class ProjectSession;
    class SceneDocument;

    [[nodiscard]] Comet::AssetScanReport move_project_asset(EditorAssets& assets,
        Comet::Project& project, SceneDocument& document, ProjectSession& session,
        Comet::AssetHandle handle, const std::filesystem::path& destination);
    [[nodiscard]] Comet::AssetScanReport remove_project_asset(EditorAssets& assets,
        const Comet::Project& project, const SceneDocument& document, Comet::AssetHandle handle);
}
