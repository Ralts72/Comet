#include <gtest/gtest.h>

#include "graphics/render_pass.h"

namespace Comet::Tests {

TEST(RenderSubPassTest, ResolvesToPresentationByDefault) {
    const RenderSubPass sub_pass;

    EXPECT_EQ(sub_pass.resolve_final_layout, ImageLayout::PresentSrcKHR);
    EXPECT_EQ(sub_pass.resolve_usage, ImageUsage::ColorAttachment);
}

TEST(RenderSubPassTest, StoresSampledResolveConfiguration) {
    RenderSubPass sub_pass;
    sub_pass.resolve_final_layout = ImageLayout::ShaderReadOnlyOptimal;
    sub_pass.resolve_usage |= ImageUsage::Sampled;

    EXPECT_EQ(sub_pass.resolve_final_layout, ImageLayout::ShaderReadOnlyOptimal);
    EXPECT_TRUE(static_cast<bool>(sub_pass.resolve_usage & ImageUsage::ColorAttachment));
    EXPECT_TRUE(static_cast<bool>(sub_pass.resolve_usage & ImageUsage::Sampled));
}

} // namespace Comet::Tests
