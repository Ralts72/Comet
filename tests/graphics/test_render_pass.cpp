#include "graphics/render_pass.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "render/render_target.h"
#include "support/engine_fixture.h"

#include <type_traits>

namespace Comet::Tests {
    static_assert(!std::is_constructible_v<RenderPass, Device&>);
    using RenderPassTest = EngineTest;

    TEST_F(RenderPassTest, RejectsInvalidAttachmentReferencesAndUnsupportedSubpassShapes) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        const std::vector attachments{Attachment::get_color_attachment(Format::R8G8B8A8_UNORM)};
        auto no_subpass = RenderPass::create(device, attachments);
        ASSERT_FALSE(no_subpass);
        EXPECT_FALSE(no_subpass.error().result.has_value());

        for(const auto& subpass : {RenderSubPass{.input_attachments = {SubpassInputAttachment(1)}},
                RenderSubPass{.color_attachments = {SubpassColorAttachment(1)}},
                RenderSubPass{.depth_stencil_attachments = {SubpassDepthStencilAttachment(1)}},
                RenderSubPass{.depth_stencil_attachments = {SubpassDepthStencilAttachment(0),
                                  SubpassDepthStencilAttachment(0)}},
                RenderSubPass{
                    .color_attachments = {SubpassColorAttachment(0), SubpassColorAttachment(0)},
                    .sample_count = SampleCount::Count4}}) {
            auto result = RenderPass::create(device, attachments, {subpass});
            ASSERT_FALSE(result);
            EXPECT_FALSE(result.error().result.has_value());
        }
        auto valid = RenderPass::create(
            device, attachments, {RenderSubPass{.color_attachments = {SubpassColorAttachment(0)}}});
        ASSERT_TRUE(valid) << valid.error();
        EXPECT_TRUE(valid.value()->get());
        EXPECT_EQ(valid.value()->get_subpass_count(), 1u);
    }

    TEST_F(RenderPassTest, FailedTargetDoesNotReplaceSwapchainGeneration) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& swapchain = context.get_swapchain();
        const auto generation = swapchain.get_active_generation();
        auto empty = RenderPass::create(device, {}, {RenderSubPass{}});
        ASSERT_TRUE(empty) << empty.error();
        auto rejected = RenderTarget::create_swapchain_target(device, *empty.value(), swapchain);
        ASSERT_FALSE(rejected);
        EXPECT_EQ(swapchain.get_active_generation(), generation);

        const auto format = generation->get_images().front()->get_info().format;
        auto pass = RenderPass::create(device, {}, {}, format);
        ASSERT_TRUE(pass) << pass.error();
        auto target = RenderTarget::create_swapchain_target(device, *pass.value(), swapchain);
        ASSERT_TRUE(target) << target.error();
        for(uint32_t i = 0; i < generation->get_images().size(); ++i) {
            EXPECT_TRUE(target.value()->get_framebuffer(i));
            EXPECT_EQ(target.value()->get_color_view(i)->get_image(), generation->get_images()[i]);
        }
        target.value().reset();
        EXPECT_EQ(swapchain.get_active_generation(), generation);
    }
}
