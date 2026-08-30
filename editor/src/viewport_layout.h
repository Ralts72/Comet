#pragma once

#include "core/math_utils.h"

namespace CometEditor {
    struct ViewportRect {
        Comet::Math::Vec2 min{};
        Comet::Math::Vec2 max{};

        [[nodiscard]] Comet::Math::Vec2 size() const {
            return max - min;
        }

        [[nodiscard]] bool contains(const Comet::Math::Vec2 point) const {
            return point.x >= min.x && point.y >= min.y
                && point.x < max.x && point.y < max.y;
        }
    };

    struct ViewportLayoutInput {
        Comet::Math::Vec2 content_origin{};
        Comet::Math::Vec2 content_size{};
        Comet::Math::Vec2 framebuffer_scale{1.0f, 1.0f};
        Comet::Math::Vec2u current_render_resolution{};
    };

    struct ViewportLayout {
        Comet::Math::Vec2 panel_content_size{};
        Comet::Math::Vec2u render_resolution{};
        ViewportRect image_display_rect;
    };

    [[nodiscard]] ViewportLayout calculate_viewport_layout(
        const ViewportLayoutInput& input);
}
