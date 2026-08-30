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

        Comet::Math::Vec2u fit_aspect_resolution(
            const Comet::Math::Vec2u available,
            const double aspect) {
            if(available.x == 0 || available.y == 0
               || !std::isfinite(aspect) || aspect <= 0.0) {
                return {};
            }

            const double available_aspect =
                static_cast<double>(available.x)
                / static_cast<double>(available.y);
            if(available_aspect > aspect) {
                return {
                    physical_extent(
                        static_cast<float>(available.y * aspect), 1.0f),
                    available.y
                };
            }
            return {
                available.x,
                physical_extent(
                    static_cast<float>(available.x / aspect), 1.0f)
            };
        }

        Comet::Math::Vec2 fit_display_size(
            const Comet::Math::Vec2 panel_size,
            const Comet::Math::Vec2u resolution) {
            const float image_aspect =
                static_cast<float>(resolution.x)
                / static_cast<float>(resolution.y);
            Comet::Math::Vec2 display_size = panel_size;
            if(display_size.x / image_aspect < display_size.y) {
                display_size.y = display_size.x / image_aspect;
            } else {
                display_size.x = display_size.y * image_aspect;
            }
            return display_size;
        }

        Comet::Math::Vec2u constrain_render_resolution(
            const Comet::Math::Vec2u resolution,
            const std::uint32_t max_dimension) {
            if(resolution.x == 0 || resolution.y == 0
               || max_dimension == 0
               || (resolution.x <= max_dimension
                   && resolution.y <= max_dimension)) {
                return resolution;
            }

            if(resolution.x >= resolution.y) {
                return {
                    max_dimension,
                    static_cast<std::uint32_t>(std::max(
                        1.0,
                        std::floor(
                            static_cast<double>(resolution.y)
                            * static_cast<double>(max_dimension)
                            / static_cast<double>(resolution.x))))
                };
            }
            return {
                static_cast<std::uint32_t>(std::max(
                    1.0,
                    std::floor(
                        static_cast<double>(resolution.x)
                        * static_cast<double>(max_dimension)
                        / static_cast<double>(resolution.y)))),
                max_dimension
            };
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
        const Comet::Math::Vec2u free_resolution{
            physical_extent(layout.panel_content_size.x, scale.x),
            physical_extent(layout.panel_content_size.y, scale.y)
        };
        switch(input.resolution_policy.mode) {
            case ViewportResolutionMode::Aspect16By9:
                layout.render_resolution = fit_aspect_resolution(
                    free_resolution, 16.0 / 9.0);
                break;
            case ViewportResolutionMode::Fixed:
                layout.render_resolution =
                    input.resolution_policy.fixed_resolution.x > 0
                        && input.resolution_policy.fixed_resolution.y > 0
                    ? input.resolution_policy.fixed_resolution
                    : Comet::Math::Vec2u{};
                break;
            default:
                layout.render_resolution = free_resolution;
                break;
        }
        layout.render_resolution = constrain_render_resolution(
            layout.render_resolution,
            input.max_render_dimension);

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

        const Comet::Math::Vec2 display_size =
            input.display_mode == ViewportDisplayMode::OneToOne
            ? Comet::Math::Vec2(
                static_cast<float>(display_resolution.x) / scale.x,
                static_cast<float>(display_resolution.y) / scale.y)
            : fit_display_size(
                layout.panel_content_size, display_resolution);
        const Comet::Math::Vec2 offset =
            (layout.panel_content_size - display_size) * 0.5f;
        layout.image_display_rect = {
            .min = input.content_origin + offset,
            .max = input.content_origin + offset + display_size
        };
        return layout;
    }
}
