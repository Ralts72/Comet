#pragma once

#include "scene/scene_document.h"

#include <optional>
#include <span>
#include <string>

namespace CometEditor {
    [[nodiscard]] std::optional<SceneDocument::Decision> draw_unsaved_scene_dialog(
        bool needs_confirmation);
    [[nodiscard]] std::optional<bool> draw_material_template_dialog(bool pending,
        const std::string& template_name, std::span<const std::string> discarded_properties);
}
