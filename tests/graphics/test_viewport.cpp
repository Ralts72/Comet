#include "graphics/pipeline/pipeline.h"
#include "graphics/vk_common.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        float viewport_y(const vk::Viewport& viewport, const float ndc_y) {
            return viewport.y + viewport.height * (ndc_y + 1.0f) * 0.5f;
        }
    }

    TEST(VulkanViewportTest, MapsPositiveNdcYToFramebufferTop) {
        constexpr float width = 1280.0f;
        constexpr float height = 720.0f;

        const vk::Viewport viewport = Graphics::get_viewport(width, height);

        EXPECT_FLOAT_EQ(viewport.x, 0.0f);
        EXPECT_FLOAT_EQ(viewport.y, height);
        EXPECT_FLOAT_EQ(viewport.width, width);
        EXPECT_FLOAT_EQ(viewport.height, -height);
        EXPECT_FLOAT_EQ(viewport_y(viewport, 1.0f), 0.0f);
        EXPECT_FLOAT_EQ(viewport_y(viewport, -1.0f), height);
        EXPECT_FLOAT_EQ(viewport.minDepth, 0.0f);
        EXPECT_FLOAT_EQ(viewport.maxDepth, 1.0f);
    }

    TEST(PipelineRasterizationStateTest, DefaultsToClockwiseForFlippedViewport) {
        const PipelineRasterizationState state;

        EXPECT_EQ(state.front_face, FrontFace::CW);
    }
}
