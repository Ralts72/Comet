#include <gtest/gtest.h>

#include "viewport_layout.h"

#include <limits>

namespace CometEditor::Tests {
    namespace {
        void expect_vec2(
            const Comet::Math::Vec2& actual, const Comet::Math::Vec2& expected) {
            EXPECT_FLOAT_EQ(actual.x, expected.x);
            EXPECT_FLOAT_EQ(actual.y, expected.y);
        }
    }

    TEST(ViewportLayoutTest, ConvertsLogicalContentToPhysicalResolution) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(20.0f, 30.0f),
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f, 2.0f),
        });

        expect_vec2(layout.panel_content_size, Comet::Math::Vec2(800.0f, 600.0f));
        EXPECT_EQ(layout.render_resolution, Comet::Math::Vec2u(1600, 1200));
        expect_vec2(layout.image_display_rect.min, Comet::Math::Vec2(20.0f, 30.0f));
        expect_vec2(layout.image_display_rect.max, Comet::Math::Vec2(820.0f, 630.0f));
    }

    TEST(ViewportLayoutTest, FitsCurrentImageInsidePanelWithLetterboxing) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(100.0f, 200.0f),
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .current_render_resolution = Comet::Math::Vec2u(1600, 900),
        });

        EXPECT_EQ(layout.render_resolution, Comet::Math::Vec2u(800, 600));
        expect_vec2(layout.image_display_rect.min, Comet::Math::Vec2(100.0f, 275.0f));
        expect_vec2(layout.image_display_rect.max, Comet::Math::Vec2(900.0f, 725.0f));
        EXPECT_TRUE(
            layout.image_display_rect.contains(Comet::Math::Vec2(500.0f, 500.0f)));
        EXPECT_FALSE(
            layout.image_display_rect.contains(Comet::Math::Vec2(500.0f, 250.0f)));
    }

    TEST(ViewportLayoutTest, SupportsNonUniformFramebufferScale) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f, 1.0f),
        });

        EXPECT_EQ(layout.render_resolution, Comet::Math::Vec2u(1600, 600));
        expect_vec2(layout.image_display_rect.min, Comet::Math::Vec2(0.0f, 150.0f));
        expect_vec2(layout.image_display_rect.max, Comet::Math::Vec2(800.0f, 450.0f));
    }

    TEST(ViewportLayoutTest, FitsSixteenByNineResolutionInsidePhysicalPanel) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(1000.0f, 800.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f),
            .resolution_policy =
                {
                    .mode = ViewportLayout::ResolutionPolicy::Mode::Aspect16By9,
                },
        });

        EXPECT_EQ(layout.render_resolution, Comet::Math::Vec2u(2000, 1125));
        expect_vec2(layout.image_display_rect.min, Comet::Math::Vec2(0.0f, 118.75f));
        expect_vec2(layout.image_display_rect.max, Comet::Math::Vec2(1000.0f, 681.25f));
    }

    TEST(ViewportLayoutTest, FixedResolutionDoesNotFollowPanelSize) {
        const ViewportLayout::ResolutionPolicy fixed{
            .mode = ViewportLayout::ResolutionPolicy::Mode::Fixed,
            .fixed_resolution = Comet::Math::Vec2u(1920, 1080),
        };
        const ViewportLayout small = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(640.0f, 480.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f),
            .resolution_policy = fixed,
        });
        const ViewportLayout large = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(1200.0f, 900.0f),
            .framebuffer_scale = Comet::Math::Vec2(1.0f),
            .resolution_policy = fixed,
        });

        EXPECT_EQ(small.render_resolution, Comet::Math::Vec2u(1920, 1080));
        EXPECT_EQ(large.render_resolution, Comet::Math::Vec2u(1920, 1080));
    }

    TEST(ViewportLayoutTest, ConstrainsFreeResolutionWithoutChangingAspect) {
        const ViewportLayout landscape = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(8000.0f, 4000.0f),
            .max_render_dimension = 4096,
        });
        const ViewportLayout portrait = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(2000.0f, 8000.0f),
            .max_render_dimension = 4096,
        });

        EXPECT_EQ(landscape.render_resolution, Comet::Math::Vec2u(4096, 2048));
        EXPECT_EQ(portrait.render_resolution, Comet::Math::Vec2u(1024, 4096));
    }

    TEST(ViewportLayoutTest, ConstrainsPolicyResultRatherThanPanelInput) {
        const ViewportLayout aspect = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(8000.0f, 6000.0f),
            .max_render_dimension = 4096,
            .resolution_policy =
                {
                    .mode = ViewportLayout::ResolutionPolicy::Mode::Aspect16By9,
                },
        });
        const ViewportLayout fixed = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .max_render_dimension = 4096,
            .resolution_policy =
                {
                    .mode = ViewportLayout::ResolutionPolicy::Mode::Fixed,
                    .fixed_resolution = Comet::Math::Vec2u(8192, 4096),
                },
        });

        EXPECT_EQ(aspect.render_resolution, Comet::Math::Vec2u(4096, 2304));
        EXPECT_EQ(fixed.render_resolution, Comet::Math::Vec2u(4096, 2048));
    }

    TEST(ViewportLayoutTest, RejectsEmptyFixedResolution) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .resolution_policy =
                {
                    .mode = ViewportLayout::ResolutionPolicy::Mode::Fixed,
                },
        });

        EXPECT_EQ(layout.render_resolution, Comet::Math::Vec2u(0));
        EXPECT_EQ(layout.image_display_rect.size(), Comet::Math::Vec2(0.0f));
    }

    TEST(ViewportLayoutTest, OneToOneUsesOnePhysicalPixelPerDisplayPixel) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(100.0f, 200.0f),
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f),
            .current_render_resolution = Comet::Math::Vec2u(1920, 1080),
            .resolution_policy =
                {
                    .mode = ViewportLayout::ResolutionPolicy::Mode::Fixed,
                    .fixed_resolution = Comet::Math::Vec2u(1920, 1080),
                },
            .display_mode = ViewportLayout::DisplayMode::OneToOne,
        });

        expect_vec2(layout.image_display_rect.min, Comet::Math::Vec2(100.0f, 230.0f));
        expect_vec2(layout.image_display_rect.max, Comet::Math::Vec2(1060.0f, 770.0f));
    }

    TEST(ViewportLayoutTest, OneToOneDoesNotCoverContentBeforeItsOrigin) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(100.0f, 200.0f),
            .content_size = Comet::Math::Vec2(800.0f, 400.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f),
            .current_render_resolution = Comet::Math::Vec2u(1920, 1080),
            .display_mode = ViewportLayout::DisplayMode::OneToOne,
        });

        expect_vec2(layout.image_display_rect.min, Comet::Math::Vec2(100.0f, 200.0f));
        expect_vec2(layout.image_display_rect.max, Comet::Math::Vec2(1060.0f, 740.0f));
    }

    TEST(ViewportLayoutTest, SanitizesInvalidInputWithoutCreatingPixels) {
        const float invalid = std::numeric_limits<float>::quiet_NaN();
        const ViewportLayout empty = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(10.0f, 20.0f),
            .content_size = Comet::Math::Vec2(-100.0f, invalid),
            .framebuffer_scale = Comet::Math::Vec2(0.0f, invalid),
        });

        EXPECT_EQ(empty.render_resolution, Comet::Math::Vec2u(0));
        expect_vec2(empty.image_display_rect.min, Comet::Math::Vec2(10.0f, 20.0f));
        expect_vec2(empty.image_display_rect.max, Comet::Math::Vec2(10.0f, 20.0f));

        const ViewportLayout fallback_scale = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(320.0f, 180.0f),
            .framebuffer_scale = Comet::Math::Vec2(0.0f, invalid),
        });
        EXPECT_EQ(fallback_scale.render_resolution, Comet::Math::Vec2u(320, 180));
    }

    TEST(ViewportLayoutTest, MapsVisibleImagePointsToCurrentPixels) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(100.0f, 200.0f),
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f),
            .current_render_resolution = Comet::Math::Vec2u(1600, 900),
        });

        EXPECT_EQ(layout.image_resolution, Comet::Math::Vec2u(1600, 900));
        EXPECT_EQ(map_viewport_point_to_pixel(layout, Comet::Math::Vec2(100.0f, 275.0f)),
            Comet::Math::Vec2u(0, 0));
        EXPECT_EQ(map_viewport_point_to_pixel(layout, Comet::Math::Vec2(500.0f, 500.0f)),
            Comet::Math::Vec2u(800, 450));
        EXPECT_EQ(map_viewport_point_to_pixel(layout, Comet::Math::Vec2(899.5f, 724.5f)),
            Comet::Math::Vec2u(1599, 899));
    }

    TEST(ViewportLayoutTest, RejectsToolbarLetterboxAndMaximumEdges) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(100.0f, 200.0f),
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .current_render_resolution = Comet::Math::Vec2u(1600, 900),
        });

        EXPECT_FALSE(
            map_viewport_point_to_pixel(layout, Comet::Math::Vec2(500.0f, 190.0f)));
        EXPECT_FALSE(
            map_viewport_point_to_pixel(layout, Comet::Math::Vec2(500.0f, 250.0f)));
        EXPECT_FALSE(
            map_viewport_point_to_pixel(layout, Comet::Math::Vec2(900.0f, 500.0f)));
        EXPECT_FALSE(
            map_viewport_point_to_pixel(layout, Comet::Math::Vec2(500.0f, 725.0f)));
    }

    TEST(ViewportLayoutTest, OneToOneMapsOnlyTheClippedVisibleRegion) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_origin = Comet::Math::Vec2(100.0f, 200.0f),
            .content_size = Comet::Math::Vec2(800.0f, 400.0f),
            .framebuffer_scale = Comet::Math::Vec2(2.0f),
            .current_render_resolution = Comet::Math::Vec2u(1920, 1080),
            .display_mode = ViewportLayout::DisplayMode::OneToOne,
        });

        expect_vec2(layout.image_display_rect.min, Comet::Math::Vec2(100.0f, 200.0f));
        expect_vec2(layout.image_display_rect.max, Comet::Math::Vec2(1060.0f, 740.0f));
        expect_vec2(layout.image_visible_rect.min, Comet::Math::Vec2(100.0f, 200.0f));
        expect_vec2(layout.image_visible_rect.max, Comet::Math::Vec2(900.0f, 600.0f));
        EXPECT_EQ(map_viewport_point_to_pixel(layout, Comet::Math::Vec2(100.0f, 200.0f)),
            Comet::Math::Vec2u(0, 0));
        EXPECT_EQ(map_viewport_point_to_pixel(layout, Comet::Math::Vec2(899.5f, 599.5f)),
            Comet::Math::Vec2u(1599, 799));
        EXPECT_FALSE(
            map_viewport_point_to_pixel(layout, Comet::Math::Vec2(950.0f, 650.0f)));
    }

    TEST(ViewportLayoutTest, UsesDisplayedImageInsteadOfPendingResolution) {
        const ViewportLayout layout = calculate_viewport_layout({
            .content_size = Comet::Math::Vec2(800.0f, 600.0f),
            .current_render_resolution = Comet::Math::Vec2u(1600, 900),
            .resolution_policy =
                {
                    .mode = ViewportLayout::ResolutionPolicy::Mode::Fixed,
                    .fixed_resolution = Comet::Math::Vec2u(1280, 720),
                },
        });

        EXPECT_EQ(layout.render_resolution, Comet::Math::Vec2u(1280, 720));
        EXPECT_EQ(layout.image_resolution, Comet::Math::Vec2u(1600, 900));
        EXPECT_EQ(map_viewport_point_to_pixel(layout, Comet::Math::Vec2(400.0f, 300.0f)),
            Comet::Math::Vec2u(800, 450));
    }
}
