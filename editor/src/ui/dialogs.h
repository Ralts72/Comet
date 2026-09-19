#pragma once

#include "scene/scene_document.h"

#include <optional>

namespace CometEditor {
    [[nodiscard]] std::optional<SceneDocument::Decision> draw_unsaved_scene_dialog(
        bool needs_confirmation);
}
