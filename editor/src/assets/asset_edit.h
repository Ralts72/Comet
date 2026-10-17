#pragma once

#include "asset/handle.h"
#include "asset/data/terrain_data.h"
#include "animation/animation_controller.h"
#include "asset/data/animation_library.h"
#include "asset/import_settings.h"
#include "asset/data/material_data.h"

#include <variant>

namespace CometEditor {
    struct MaterialEdit {
        Comet::MaterialData before;
        Comet::MaterialData after;
    };

    struct TextureEdit {
        Comet::TextureImportSettings before;
        Comet::TextureImportSettings after;
    };

    struct AnimationControllerEdit {
        Comet::AnimationController before;
        Comet::AnimationController after;
    };

    struct AnimationLibraryEdit {
        Comet::AnimationLibrary before;
        Comet::AnimationLibrary after;
    };

    struct TerrainEdit {
        Comet::TerrainData before;
        Comet::TerrainData after;
    };

    struct AudioEdit {
        Comet::AudioImportSettings before;
        Comet::AudioImportSettings after;
    };

    struct AssetEdit {
        enum class Action { Apply, Preview, Commit, Cancel };
        Comet::AssetTarget target;
        std::variant<MaterialEdit, TextureEdit, AnimationControllerEdit, AnimationLibraryEdit,
            AudioEdit, TerrainEdit>
            value;
        Action action = Action::Apply;
    };
}
