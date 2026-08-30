#include "viewport_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace CometEditor {
    namespace {
        float sanitize_extent(const float value) {
            return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
        }

        float sanitize_scale(const float value) {
            return std::isfinite(value) && value > 0.0f ? value : 1.0f;
        }

        std::uint32_t physical_extent(
            const float logical_extent,
            const float framebuffer_scale) {
            if(logical_extent <= 0.0f) {
                return 0;
            }

            const double scaled = std::round(
                static_cast<double>(logical_extent)
                * static_cast<double>(framebuffer_scale));
            const double maximum = static_cast<double>(
                std::numeric_limits<std::uint32_t>::max());
            return static_cast<std::uint32_t>(
                std::clamp(scaled, 1.0, maximum));
        }
    }

    ViewportLayout calculate_viewport_layout(
        const ViewportLayoutInput& input) {
        ViewportLayout layout;
        layout.panel_content_size = {
            sanitize_extent(input.content_size.x),
            sanitize_extent(input.content_size.y)
        };
        const Comet::Math::Vec2 scale{
            sanitize_scale(input.framebuffer_scale.x),
            sanitize_scale(input.framebuffer_scale.y)
        };
        layout.render_resolution = {
            physical_extent(layout.panel_content_size.x, scale.x),
            physical_extent(layout.panel_content_size.y, scale.y)
        };

        const Comet::Math::Vec2u display_resolution =
            input.current_render_resolution.x > 0
                && input.current_render_resolution.y > 0
            ? input.current_render_resolution
            : layout.render_resolution;
        if(layout.panel_content_size.x <= 0.0f
           || layout.panel_content_size.y <= 0.0f
           || display_resolution.x == 0
           || display_resolution.y == 0) {
            layout.image_display_rect = {
                .min = input.content_origin,
                .max = input.content_origin
            };
            return layout;
        }

        const float image_aspect =
            static_cast<float>(display_resolution.x)
            / static_cast<float>(display_resolution.y);
        Comet::Math::Vec2 display_size = layout.panel_content_size;
        if(display_size.x / image_aspect < display_size.y) {
            display_size.y = display_size.x / image_aspect;
        } else {
            display_size.x = display_size.y * image_aspect;
        }
        const Comet::Math::Vec2 offset =
            (layout.panel_content_size - display_size) * 0.5f;
        layout.image_display_rect = {
            .min = input.content_origin + offset,
            .max = input.content_origin + offset + display_size
        };
        return layout;
    }
}
