#ifdef COMET_TEST_SHARED_UI
#include "imgui_context.h"
#include "imgui_hdr_frag.h"
#include "support/render_gpu_test.h"
#include "common/scope_exit.h"
#include "core/window.h"
#include "graphics/frame_buffer.h"
#include "graphics/swapchain.h"
#include "render/scene/render_submission.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/packing.hpp>
#include <imgui_impl_vulkan.h>

#include <array>
#include <cmath>
#include <cstring>

namespace Comet::Tests {
    namespace {
        Result<std::unique_ptr<RenderPass>, GraphicsError> load_pass(
            Device& device, Format format) {
            auto color = Attachment::get_color_attachment(format);
            color.description.load_op = AttachmentLoadOp::Load;
            color.description.store_op = AttachmentStoreOp::Store;
            color.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
            color.description.final_layout = ImageLayout::ShaderReadOnlyOptimal;
            color.usage |= ImageUsage::Sampled;
            color.usage |= ImageUsage::CopySrc;
            return RenderPass::create(
                device, {color}, {{.color_attachments = {SubpassColorAttachment(0)}}}, format);
        }

        void prepare_overlay(const CommandBuffer& command, const Image& image,
            vk::ImageLayout previous = vk::ImageLayout::eShaderReadOnlyOptimal) {
            vk::ImageMemoryBarrier2 barrier;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentRead
                                    | vk::AccessFlagBits2::eColorAttachmentWrite;
            barrier.oldLayout = previous;
            barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.get();
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            command.get().pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barrier));
        }

        void transparent_window() {
            ImGui::SetNextWindowPos({16, 16});
            ImGui::SetNextWindowSize({64, 64});
            ImGui::SetNextWindowBgAlpha(0.5f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 0, 0, 1));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            ImGui::Begin("Transparent overlay", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                    | ImGuiWindowFlags_NoInputs);
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        }

        std::array<int, 4> byte_pixel(
            const std::vector<std::byte>& bytes, Math::Vec2u size, Math::Vec2u point) {
            const auto offset = (std::size_t(point.y) * size.x + point.x) * 4;
            return {std::to_integer<int>(bytes[offset]), std::to_integer<int>(bytes[offset + 1]),
                std::to_integer<int>(bytes[offset + 2]), std::to_integer<int>(bytes[offset + 3])};
        }

        Math::Vec4 half_pixel(
            const std::vector<std::byte>& bytes, Math::Vec2u size, Math::Vec2u point) {
            Math::Vec4 pixel{};
            const auto offset = (std::size_t(point.y) * size.x + point.x) * 8;
            for(Math::Vec4::length_type channel = 0; channel < pixel.length(); ++channel) {
                std::uint16_t value;
                std::memcpy(&value, bytes.data() + offset + channel * 2, sizeof(value));
                pixel[channel] = glm::unpackHalf1x16(value);
            }
            return pixel;
        }
    }

    class ImGuiOverlayGpuTest: public RenderGpuTest {
    protected:
        std::unique_ptr<CometUi::ImGuiContext> ui;

        void create_ui() {
            auto created = CometUi::ImGuiContext::create(engine->get_window(),
                engine->get_renderer().get_render_context(),
                {.composition = CometUi::ImGuiContext::Composition::Preserve});
            ASSERT_TRUE(created) << created.error();
            ui = std::move(created).value();
        }

        void TearDown() override {
            if(engine) {
                engine->get_renderer().set_overlay({});
                engine->get_renderer().wait_idle();
            }
            ui.reset();
            RenderGpuTest::TearDown();
        }

        Result<void, GraphicsError> presentation_frame(bool draw_window) {
            auto& renderer = engine->get_renderer();
            for(unsigned attempt = 0; attempt < 12; ++attempt) {
                engine->get_window().poll_events();
                auto prepared = renderer.prepare_frame();
                if(!prepared)
                    return Result<void, GraphicsError>::failure(prepared.error());
                if(prepared.value() != Renderer::FramePreparation::Ready)
                    continue;
                if(ui) {
                    if(!ui->begin_frame())
                        return Result<void, GraphicsError>::failure({"UI frame unavailable"});
                    if(draw_window)
                        transparent_window();
                    ui->end_frame();
                }
                RenderScene scene;
                scene.environment.background_color = {0.3f, 0.1f, 0.05f};
                scene.cameras.push_back(RenderCamera{.primary = true});
                return renderer.render_frame(scene);
            }
            return Result<void, GraphicsError>::failure({"Presentation remained deferred"});
        }
    };

    TEST_F(ImGuiOverlayGpuTest, SceneThenPreservingUiSurvivesResizeBackendRebuildAndUnload) {
        create_ui();
        ASSERT_TRUE(ui);
        auto& renderer = engine->get_renderer();
        auto& swapchain = renderer.get_render_context().get_swapchain();
        EXPECT_FALSE(renderer.get_scene_renderer().is_offscreen());
        EXPECT_EQ(ImGui::GetIO().IniFilename, nullptr);
        EXPECT_FALSE(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable);
        unsigned rendered = 0;
        unsigned released = 0;
        unsigned rebuilt = 0;
        renderer.set_overlay({.render =
                                  [&](CommandBuffer& command) {
                                      ++rendered;
                                      ui->render(command);
                                  },
            .release =
                [&] {
                    ++released;
                    ui->release_swapchain_resources();
                },
            .rebuild =
                [&](const SwapchainCompatibility& compatibility) {
                    ++rebuilt;
                    return ui->rebuild_swapchain_resources(compatibility);
                }});
        ASSERT_TRUE(presentation_frame(false));
        ASSERT_TRUE(presentation_frame(true));
        const auto old_size = engine->get_window().get_framebuffer_size();
        glfwSetWindowSize(engine->get_window().get(), 240, 180);
        renderer.request_swapchain_recreation();
        ASSERT_TRUE(presentation_frame(true));
        const auto new_size = engine->get_window().get_framebuffer_size();
        EXPECT_NE(new_size, old_size);
        EXPECT_EQ(swapchain.get_width(), new_size.x);
        EXPECT_EQ(swapchain.get_height(), new_size.y);
        EXPECT_GT(released, 0u);
        EXPECT_EQ(rebuilt, released);
        EXPECT_EQ(rendered, 3u);

        renderer.wait_idle();
        ui->release_swapchain_resources();
        ASSERT_TRUE(ui->rebuild_swapchain_resources({.image_count_changed = true}));
        ASSERT_TRUE(presentation_frame(true));
        renderer.set_overlay({});
        renderer.wait_idle();
        ui.reset();
        const auto before_unloaded_frame = rendered;
        ASSERT_TRUE(presentation_frame(false));
        EXPECT_EQ(rendered, before_unloaded_frame);
    }

    TEST_F(ImGuiOverlayGpuTest, CompatibleOffscreenBackendKeepsEmptyUiAndPixelsOutsideWindow) {
        create_ui();
        ASSERT_TRUE(ui);
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        const auto size = engine->get_window().get_framebuffer_size();
        const auto format = context.get_swapchain().get_images().front()->get_info().format;
        Config config;
        config.vulkan.surface_format = format;
        config.vulkan.msaa_samples = SampleCount::Count1;
        MaterialPrograms programs(engine->get_asset_registry());
        auto scene_owner = SceneRenderer::create(
            device, programs, engine->get_render_resources(), config.vulkan, config.render, size);
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        ASSERT_EQ(scene.get_offscreen_color_view(0)->get_image()->get_info().format, format);
        auto overlay_owner = load_pass(device, format);
        ASSERT_TRUE(overlay_owner) << overlay_owner.error();
        std::shared_ptr<RenderPass> overlay = std::move(overlay_owner).value();
        // 离屏 pass 的依赖与呈现 pass 不同，管线也须按实际测试 pass 创建。
        ImGui_ImplVulkan_PipelineInfo pipeline{};
        pipeline.RenderPass = overlay->get();
        pipeline.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_CreateMainPipeline(&pipeline);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        RenderSubmission submission;
        submission.environment.background_color = {0.2f, 0.4f, 0.8f};
        std::array<std::shared_ptr<Readback>, 3> outputs;
        Math::Vec2u inside{};
        for(std::size_t index = 0; index < outputs.size(); ++index) {
            ASSERT_TRUE(ui->begin_frame());
            if(index == 2)
                transparent_window();
            const auto scale = ImGui::GetIO().DisplayFramebufferScale;
            inside = {static_cast<unsigned>(40 * scale.x), static_cast<unsigned>(40 * scale.y)};
            ui->end_frame();
            if(index == 1)
                EXPECT_EQ(ImGui::GetDrawData()->CmdListsCount, 0);
            if(index == 2)
                EXPECT_GT(ImGui::GetDrawData()->CmdListsCount, 0);
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            const auto slot = frames.get_current_frame_slot_index();
            const auto image = scene.get_offscreen_color_view(slot)->get_image();
            const auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            if(index != 0) {
                // 交换链没有 CopySrc；离屏目标复用实际后端，但不替代生产 loadOp 的路径测试。
                auto& command = frames.get_current_command_buffer();
                prepare_overlay(command, *image);
                auto framebuffer = FrameBuffer::try_create(
                    device, *overlay, {scene.get_offscreen_color_view(slot)}, size.x, size.y);
                ASSERT_TRUE(framebuffer) << framebuffer.error();
                frames.retain_current_frame_resource(overlay);
                frames.retain_current_frame_resource(framebuffer.value());
                command.begin_render_pass(*overlay, *framebuffer.value(), {});
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command.get());
                command.end_render_pass();
            }
            outputs[index] = std::make_shared<Readback>(device,
                context.get_context().get_physical_device(), std::uint64_t(size.x) * size.y * 4);
            ASSERT_TRUE(outputs[index]->get());
            copy_output(frames, image, outputs[index], size);
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const auto baseline = outputs[0]->read();
        const auto empty = outputs[1]->read();
        const auto window = outputs[2]->read();
        EXPECT_EQ(empty, baseline);
        ASSERT_LT(inside.x, size.x);
        ASSERT_LT(inside.y, size.y);
        const auto before = byte_pixel(baseline, size, inside);
        const auto after = byte_pixel(window, size, inside);
        EXPECT_NE(before, after);
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const auto red = bgra ? 2 : 0;
        const auto blue = bgra ? 0 : 2;
        EXPECT_GT(after[red], before[red]);
        EXPECT_GT(after[blue], 0);
        EXPECT_LT(after[blue], before[blue]);
        for(const auto point :
            {Math::Vec2u{4, 4}, Math::Vec2u{size.x - 4, 4}, Math::Vec2u{size.x - 4, size.y - 4}})
            EXPECT_EQ(byte_pixel(window, size, point), byte_pixel(baseline, size, point));
    }

    TEST_F(
        ImGuiOverlayGpuTest, ActualHdrFragmentDecodesRgbWithoutDecodingAlphaOrClippingBackground) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        constexpr Math::Vec2u size{64, 32};
        const Math::Vec4 background{2, 0.4f, 0.1f, 0.25f};
        auto color = Attachment::get_color_attachment(Format::R16G16B16A16_SFLOAT);
        color.description.store_op = AttachmentStoreOp::Store;
        color.description.final_layout = ImageLayout::ColorAttachmentOptimal;
        color.usage |= ImageUsage::Sampled;
        color.usage |= ImageUsage::CopySrc;
        auto clear_pass = RenderPass::create(device, {color},
            {{.color_attachments = {SubpassColorAttachment(0)}}}, Format::R16G16B16A16_SFLOAT);
        ASSERT_TRUE(clear_pass) << clear_pass.error();
        auto output_owner =
            RenderTarget::try_create_multi_target(device, *clear_pass.value(), size, 2);
        ASSERT_TRUE(output_owner) << output_owner.error();
        auto output = std::move(output_owner).value();
        output->set_clear_value(ClearValue(background));
        auto overlay = load_pass(device, Format::R16G16B16A16_SFLOAT);
        ASSERT_TRUE(overlay) << overlay.error();

        auto* imgui = ImGui::CreateContext();
        const ScopeExit cleanup([&] {
            device.wait_idle_for_shutdown();
            if(ImGui::GetIO().BackendRendererUserData)
                ImGui_ImplVulkan_Shutdown();
            ImGui::DestroyContext(imgui);
        });
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {float(size.x), float(size.y)};
        io.DeltaTime = 1.0f / 60;
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_0;
        info.Instance = context.get_context().instance();
        info.PhysicalDevice = context.get_context().get_physical_device();
        info.Device = device.get();
        info.QueueFamily =
            context.get_context().get_graphics_queue_family().queue_family_index.value();
        info.Queue = device.get_graphics_queue().get();
        info.DescriptorPoolSize = 16;
        info.MinImageCount = info.ImageCount = 2;
        info.PipelineInfoMain.RenderPass = overlay.value()->get();
        info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.CustomShaderFragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.CustomShaderFragCreateInfo.codeSize = IMGUI_HDR_FRAG.size() * sizeof(std::uint32_t);
        info.CustomShaderFragCreateInfo.pCode = IMGUI_HDR_FRAG.data();
        ASSERT_TRUE(ImGui_ImplVulkan_Init(&info));
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            {0, 0}, {16, 32}, IM_COL32(128, 128, 128, 255));
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            {16, 0}, {32, 32}, IM_COL32(128, 128, 128, 128));
        ImGui::Render();

        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        auto& command = frames.get_current_command_buffer();
        const auto slot = frames.get_current_frame_slot_index();
        output->begin_render_target(command, slot);
        output->end_render_target(command);
        const auto image = output->get_color_view(slot)->get_image();
        prepare_overlay(command, *image, vk::ImageLayout::eColorAttachmentOptimal);
        auto framebuffer = FrameBuffer::try_create(
            device, *overlay.value(), {output->get_color_view(slot)}, size.x, size.y);
        ASSERT_TRUE(framebuffer) << framebuffer.error();
        frames.retain_current_frame_resource(framebuffer.value());
        command.begin_render_pass(*overlay.value(), *framebuffer.value(), {});
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command.get());
        command.end_render_pass();
        auto readback = std::make_shared<Readback>(
            device, context.get_context().get_physical_device(), size.x * size.y * 8);
        ASSERT_TRUE(readback->get());
        copy_output(frames, image, readback, size);
        submit(device, frames);
        frames.wait_for_all_slots();
        const auto pixels = readback->read();
        const auto opaque = half_pixel(pixels, size, {8, 16});
        const auto blended = half_pixel(pixels, size, {24, 16});
        const auto outside = half_pixel(pixels, size, {48, 16});
        const float alpha = 128.0f / 255;
        const float linear_gray = std::pow((alpha + 0.055f) / 1.055f, 2.4f);
        for(std::size_t channel = 0; channel < 3; ++channel) {
            EXPECT_NEAR(opaque[channel], linear_gray, 0.002f);
            EXPECT_NEAR(
                blended[channel], linear_gray * alpha + background[channel] * (1 - alpha), 0.003f);
            EXPECT_NEAR(outside[channel], background[channel], 0.002f);
        }
        EXPECT_NEAR(opaque[3], 1, 0.001f);
        EXPECT_NEAR(blended[3], alpha + background[3] * (1 - alpha), 0.002f);
        EXPECT_NEAR(outside[3], background[3], 0.001f);
        EXPECT_GT(outside[0], 1);
    }
}
#endif
