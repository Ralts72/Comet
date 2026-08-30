#include <gtest/gtest.h>

#include "viewport_layout.h"

#include <limits>

namespace CometEditor::Tests {
    namespace {
        void expect_vec2(
            const Comet::Math::Vec2& actual,
            const Comet::Math::Vec2& expected) {
            EXPECT_FLOAT_EQ(actual.x, expected.x);
            EXPECT_FLOAT_EQ(actual.y, expected.y);
        }
    }

    TEST(ViewportLayoutTest, ConvertsLogicalContentToPhysicalResolution) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(20.0f, 30.0f),
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f, 2.0f)
        });

        expect_vec2(
            layout.panel_content_size,
            Comet::Math::Vec2(800.0f, 600.0f));
        EXPECT_EQ(
            layout.render_resolution,
            Comet::Math::Vec2u(1600, 1200));
        expect_vec2(
            layout.image_display_rect.min,
            Comet::Math::Vec2(20.0f, 30.0f));
        expect_vec2(
            layout.image_display_rect.max,
            Comet::Math::Vec2(820.0f, 630.0f));
    }

    TEST(ViewportLayoutTest, FitsCurrentImageInsidePanelWithLetterboxing) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(100.0f, 200.0f),
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .current_render_resolution = Comet::Math::Vec2u(1600, 900)
        });

        EXPECT_EQ(
            layout.render_resolution,
            Comet::Math::Vec2u(800, 600));
        expect_vec2(
            layout.image_display_rect.min,
            Comet::Math::Vec2(100.0f, 275.0f));
        expect_vec2(
            layout.image_display_rect.max,
            Comet::Math::Vec2(900.0f, 725.0f));
        EXPECT_TRUE(layout.image_display_rect.contains(
            Comet::Math::Vec2(500.0f, 500.0f)));
        EXPECT_FALSE(layout.image_display_rect.contains(
            Comet::Math::Vec2(500.0f, 250.0f)));
    }

    TEST(ViewportLayoutTest, SupportsNonUniformFramebufferScale) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f, 1.0f)
        });

        EXPECT_EQ(
            layout.render_resolution,
            Comet::Math::Vec2u(1600, 600));
        expect_vec2(
            layout.image_display_rect.min,
            Comet::Math::Vec2(0.0f, 150.0f));
        expect_vec2(
            layout.image_display_rect.max,
            Comet::Math::Vec2(800.0f, 450.0f));
    }

    TEST(ViewportLayoutTest, SanitizesInvalidInputWithoutCreatingPixels) {
        const float invalid = std::numeric_limits<float>::quiet_NaN();
        const ViewportLayout empty = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(10.0f, 20.0f),
            .content_size = Comet::Math::Vec2(-100.0f, invalid),
            .framebuffer_scale = Comet::Math::Vec2(0.0f, invalid)
        });

        EXPECT_EQ(empty.render_resolution, Comet::Math::Vec2u(0));
        expect_vec2(
            empty.image_display_rect.min,
            Comet::Math::Vec2(10.0f, 20.0f));
        expect_vec2(
            empty.image_display_rect.max,
            Comet::Math::Vec2(10.0f, 20.0f));

        const ViewportLayout fallback_scale = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(320.0f, 180.0f),
            .framebuffer_scale = Comet::Math::Vec2(0.0f, invalid)
        });
        EXPECT_EQ(
            fallback_scale.render_resolution,
            Comet::Math::Vec2u(320, 180));
    }
}
