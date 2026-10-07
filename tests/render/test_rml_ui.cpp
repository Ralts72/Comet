#include "ui/rml_renderer.h"
#include "support/render_gpu_test.h"

#include "core/window.h"
#include "graphics/command/command_buffer.h"
#include "graphics/resource/image.h"
#include "render/overlay_record_context.h"

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/RenderInterface.h>

#include <glm/gtc/packing.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <functional>

namespace Comet::Tests {
    namespace {
        class RmlGpuProbe: public Rml::Element {
        public:
            using Rml::Element::Element;
            std::function<void()> draw;

        protected:
            void OnRender() override {
                if(draw)
                    draw();
            }
        };

        std::array<Rml::Vertex, 4> quad(const Rml::ColourbPremultiplied colour) {
            return {{{{8, 8}, colour, {0, 0}}, {{24, 8}, colour, {1, 0}},
                {{24, 24}, colour, {1, 1}}, {{8, 24}, colour, {0, 1}}}};
        }

        constexpr std::array<int, 6> QUAD_INDICES{0, 1, 2, 0, 2, 3};

        Math::Vec4 pixel(const std::vector<std::byte>& bytes, const Math::Vec2u size,
            const Math::Vec2u point, const Format format) {
            Math::Vec4 value;
            const auto index = std::size_t(point.y) * size.x + point.x;
            for(std::size_t channel = 0; channel < 4; ++channel) {
                if(format == Format::R16G16B16A16_SFLOAT) {
                    std::uint16_t half;
                    std::memcpy(&half, bytes.data() + (index * 4 + channel) * 2, sizeof(half));
                    value[static_cast<int>(channel)] = glm::unpackHalf1x16(half);
                } else {
                    value[static_cast<int>(channel)] =
                        float(std::to_integer<unsigned char>(bytes[index * 4 + channel])) / 255;
                }
            }
            return value;
        }

        float decode_srgb(const float value) {
            if(value <= 0.04045f)
                return value / 12.92f;
            return std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        Math::Vec3 expected_pixel(const Math::Vec3 background, const Math::Vec3 straight_srgb,
            const float alpha, const Format format) {
            Math::Vec3 value;
            for(int channel = 0; channel < 3; ++channel) {
                if(format == Format::R8G8B8A8_UNORM) {
                    // UNORM 沿现有呈现编码域混合。
                    value[channel] =
                        straight_srgb[channel] * alpha + background[channel] * (1 - alpha);
                    continue;
                }
                value[channel] =
                    decode_srgb(straight_srgb[channel]) * alpha + background[channel] * (1 - alpha);
                if(format == Format::R8G8B8A8_SRGB)
                    value[channel] = encode_srgb(value[channel]);
            }
            return value;
        }
    }

    class RmlUiGpuTest: public RenderGpuTest {
    protected:
        std::unique_ptr<Comet::Ui::RmlRenderer> ui;
        Rml::ElementInstancerGeneric<RmlGpuProbe> probe_instancer;
        Rml::Context* context = nullptr;
        Rml::ElementDocument* document = nullptr;
        RmlGpuProbe* probe = nullptr;
        bool initialized = false;

        void SetUp() override {
            RenderGpuTest::SetUp();
            ASSERT_TRUE(engine);
            auto created = Comet::Ui::RmlRenderer::create(engine->get_renderer());
            ASSERT_TRUE(created) << created.error();
            ui = std::move(created).value();
            Rml::SetRenderInterface(&ui->interface());
            ASSERT_TRUE(Rml::Initialise());
            initialized = true;
            Rml::Factory::RegisterElementInstancer("gpu-probe", &probe_instancer);
            context = Rml::CreateContext("comet_gpu_test", {32, 32}, &ui->interface());
            ASSERT_NE(context, nullptr);
            document = context->LoadDocumentFromMemory(
                "<rml><head><style>body { margin: 0; } gpu-probe { display: block; width: 32px; "
                "height: 32px; }</style></head><body><gpu-probe id='probe'/></body></rml>");
            ASSERT_NE(document, nullptr);
            probe = dynamic_cast<RmlGpuProbe*>(document->GetElementById("probe"));
            ASSERT_NE(probe, nullptr);
            document->Show();
            ASSERT_TRUE(context->Update());
        }

        void TearDown() override {
            if(engine) {
                engine->get_renderer().set_overlay({});
                engine->get_renderer().wait_idle();
            }
            if(initialized) {
                Rml::Shutdown();
                Rml::SetRenderInterface(nullptr);
            }
            ui.reset();
            RenderGpuTest::TearDown();
        }

        Result<void, GraphicsError> presentation_frame() {
            auto& renderer = engine->get_renderer();
            for(unsigned attempt = 0; attempt < 12; ++attempt) {
                engine->get_window().poll_events();
                auto prepared = renderer.prepare_frame();
                if(!prepared)
                    return Result<void, GraphicsError>::failure(prepared.error());
                if(prepared.value() == Renderer::FramePreparation::Ready)
                    return renderer.render_frame();
            }
            return Result<void, GraphicsError>::failure({"RmlUi presentation remained deferred"});
        }

        Comet::Ui::RmlRenderer::Target target(const Format format, const Math::Vec2u size) {
            auto& renderer = engine->get_renderer();
            auto& device = renderer.get_render_context().get_device();
            auto color = Attachment::get_color_attachment(format);
            color.description.load_op = AttachmentLoadOp::Load;
            color.description.store_op = AttachmentStoreOp::Store;
            color.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
            color.description.final_layout = ImageLayout::ShaderReadOnlyOptimal;
            color.usage |= ImageUsage::CopySrc;
            color.usage |= ImageUsage::CopyDst;
            color.usage |= ImageUsage::Sampled;
            auto pass = RenderPass::create(
                device, {color}, {{.color_attachments = {SubpassColorAttachment(0)}}}, format);
            if(!pass) {
                ADD_FAILURE() << pass.error();
                return {};
            }
            std::shared_ptr<RenderPass> owner = std::move(pass).value();
            auto created = RenderTarget::try_create_multi_target(
                device, *owner, size, renderer.get_frame_scheduler().get_frame_slot_count());
            if(!created) {
                ADD_FAILURE() << created.error();
                return {};
            }
            auto color_space = ImageColorSpace::SrgbNonlinearKHR;
            if(format == Format::R16G16B16A16_SFLOAT)
                color_space = ImageColorSpace::ExtendedSrgbLinearEXT;
            return {owner, std::move(created).value(), format, color_space};
        }

        static void clear_target(OverlayRecordContext& frame, const std::shared_ptr<Image>& image,
            const Math::Vec3 color, const vk::ImageLayout previous) {
            auto& command = frame.command_buffer();
            vk::ImageMemoryBarrier2 barrier;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.srcAccessMask =
                vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.oldLayout = previous;
            barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image->get();
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            command.get().pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barrier));
            command.get().clearColorImage(image->get(), vk::ImageLayout::eTransferDstOptimal,
                vk::ClearColorValue(std::array<float, 4>{color.x, color.y, color.z, 1}),
                barrier.subresourceRange);
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentRead
                                    | vk::AccessFlagBits2::eColorAttachmentWrite;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            command.get().pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barrier));
        }

        static void copy_target(OverlayRecordContext& frame, const std::shared_ptr<Image>& image,
            const std::shared_ptr<Readback>& readback, const Math::Vec2u size) {
            auto& command = frame.command_buffer();
            vk::ImageMemoryBarrier2 barrier;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
            barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image->get();
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            command.get().pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barrier));
            vk::BufferImageCopy region;
            region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            region.imageExtent = vk::Extent3D(size.x, size.y, 1);
            command.get().copyImageToBuffer(
                image->get(), vk::ImageLayout::eTransferSrcOptimal, readback->get(), region);
            vk::BufferMemoryBarrier2 host;
            host.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            host.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            host.dstStageMask = vk::PipelineStageFlagBits2::eHost;
            host.dstAccessMask = vk::AccessFlagBits2::eHostRead;
            host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.buffer = readback->get();
            host.size = VK_WHOLE_SIZE;
            command.get().pipelineBarrier2(vk::DependencyInfo{}.setBufferMemoryBarriers(host));
            frame.retain(readback);
        }

        void expect_color(const std::vector<std::byte>& bytes, const Math::Vec2u size,
            const Math::Vec2u point, const Format format, const Math::Vec3 expected) {
            const auto actual = pixel(bytes, size, point, format);
            for(int channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(actual[channel], expected[channel], 0.012f) << channel;
            EXPECT_NEAR(actual.a, 1, 0.006f);
        }
    };

    TEST_F(RmlUiGpuTest, PremultipliedAlphaScissorTransformAndReleaseMatchSdrHdrPixels) {
        auto& renderer = engine->get_renderer();
        auto& host = renderer.get_render_context();
        auto& interface = ui->interface();
        const Math::Vec2u size{32, 32};
        for(const auto format :
            {Format::R8G8B8A8_SRGB, Format::R8G8B8A8_UNORM, Format::R16G16B16A16_SFLOAT}) {
            SCOPED_TRACE(static_cast<int>(format));
            auto output = target(format, size);
            ASSERT_TRUE(output.target);
            const auto bytes_per_pixel = format == Format::R16G16B16A16_SFLOAT ? 8 : 4;
            auto readback = std::make_shared<Readback>(host.get_device(),
                host.get_context().get_physical_device(), size.x * size.y * bytes_per_pixel);
            ASSERT_TRUE(readback->get());
            Math::Vec3 background{0.1f, 0.2f, 0.3f};
            if(format == Format::R16G16B16A16_SFLOAT)
                background.r = 2;
            const auto vertices = quad({128, 32, 0, 128});
            const auto geometry = interface.CompileGeometry(
                {vertices.data(), vertices.size()}, {QUAD_INDICES.data(), QUAD_INDICES.size()});
            ASSERT_NE(geometry, 0u);
            const std::array<Rml::byte, 4> texels{255, 255, 255, 255};
            const auto texture = interface.GenerateTexture({texels.data(), texels.size()}, {1, 1});
            ASSERT_NE(texture, 0u);
            probe->draw = [&] {
                const auto transform = Rml::Matrix4f::Translate(4, 0, 0);
                interface.SetTransform(&transform);
                interface.EnableScissorRegion(true);
                interface.SetScissorRegion(Rml::Rectanglei::FromCorners({16, 8}, {24, 24}));
                interface.RenderGeometry(geometry, {0, 0}, texture);
                // 在本帧 GPU 提交前释放；OverlayRecordContext 必须保留所有间接 owner。
                interface.ReleaseGeometry(geometry);
                interface.ReleaseTexture(texture);
                interface.SetTransform(nullptr);
                interface.EnableScissorRegion(false);
            };
            renderer.set_overlay({.render = [&](OverlayRecordContext& frame) {
                const auto image = output.target->get_color_view(frame.frame_slot())->get_image();
                clear_target(frame, image, background, vk::ImageLayout::eUndefined);
                auto rendered = ui->render_to_target(frame, *context, output);
                if(!rendered)
                    return rendered;
                copy_target(frame, image, readback, size);
                return Result<void, GraphicsError>::success();
            }});
            ASSERT_TRUE(presentation_frame());
            // ReleaseGeometry/ReleaseTexture 已发生，GPU 才在这一阶段完成。
            renderer.wait_idle();
            const auto pixels = readback->read();
            auto untouched = background;
            if(format == Format::R8G8B8A8_SRGB)
                for(int channel = 0; channel < 3; ++channel)
                    untouched[channel] = encode_srgb(untouched[channel]);
            expect_color(pixels, size, {13, 12}, format, untouched);
            expect_color(pixels, size, {25, 12}, format, untouched);
            expect_color(pixels, size, {20, 12}, format,
                expected_pixel(background, {1, 0.25f, 0}, 128.f / 255, format));
            renderer.set_overlay({});
            probe->draw = {};
        }
    }

    TEST_F(RmlUiGpuTest, LinearAtlasFilteringKeepsTransparentEdgesWithoutColorFringes) {
        auto& renderer = engine->get_renderer();
        auto& host = renderer.get_render_context();
        auto& interface = ui->interface();
        const Math::Vec2u size{32, 32};
        const Math::Vec3 background{0.1f, 0.2f, 0.3f};
        for(const auto format :
            {Format::R8G8B8A8_SRGB, Format::R8G8B8A8_UNORM, Format::R16G16B16A16_SFLOAT}) {
            SCOPED_TRACE(static_cast<int>(format));
            auto output = target(format, size);
            ASSERT_TRUE(output.target);
            const auto vertices = quad({255, 255, 255, 255});
            const auto geometry = interface.CompileGeometry(
                {vertices.data(), vertices.size()}, {QUAD_INDICES.data(), QUAD_INDICES.size()});
            ASSERT_NE(geometry, 0u);
            // 第一像素为半透明红；完全透明蓝必须归零，不能在滤波时染蓝边缘。
            const std::array<Rml::byte, 8> texels{128, 0, 0, 128, 0, 0, 255, 0};
            const auto texture = interface.GenerateTexture({texels.data(), texels.size()}, {2, 1});
            ASSERT_NE(texture, 0u);
            probe->draw = [&] { interface.RenderGeometry(geometry, {0, 0}, texture); };
            const auto stride = format == Format::R16G16B16A16_SFLOAT ? 8 : 4;
            auto readback = std::make_shared<Readback>(host.get_device(),
                host.get_context().get_physical_device(), size.x * size.y * stride);
            ASSERT_TRUE(readback->get());
            std::vector<bool> used(renderer.get_frame_scheduler().get_frame_slot_count(), false);
            renderer.set_overlay({.render = [&](OverlayRecordContext& frame) {
                const auto slot = frame.frame_slot();
                const auto image = output.target->get_color_view(slot)->get_image();
                clear_target(frame, image, background,
                    used[slot] ? vk::ImageLayout::eTransferSrcOptimal
                               : vk::ImageLayout::eUndefined);
                used[slot] = true;
                auto rendered = ui->render_to_target(frame, *context, output);
                if(!rendered)
                    return rendered;
                copy_target(frame, image, readback, size);
                return Result<void, GraphicsError>::success();
            }});
            for(unsigned frame = 0; frame < used.size() + 1; ++frame) {
                ASSERT_TRUE(presentation_frame());
                renderer.wait_idle();
                const auto pixels = readback->read();
                for(const auto x : {10u, 12u, 16u, 20u, 23u}) {
                    const float uv = (float(x) + 0.5f - 8) / 16;
                    const float coverage = 1 - std::clamp(uv * 2 - 0.5f, 0.f, 1.f);
                    expect_color(pixels, size, {x, 12}, format,
                        expected_pixel(background, {1, 0, 0}, coverage * 128.f / 255, format));
                }
            }
            renderer.set_overlay({});
            probe->draw = {};
            interface.ReleaseGeometry(geometry);
            interface.ReleaseTexture(texture);
        }
    }

    TEST_F(RmlUiGpuTest, InvalidGeometryTextureAndUnsupportedEffectsReturnExplicitErrors) {
        auto& interface = ui->interface();
        const auto vertices = quad({255, 255, 255, 255});
        const std::array<int, 3> invalid_indices{0, 1, 9};
        EXPECT_EQ(interface.CompileGeometry({vertices.data(), vertices.size()},
                      {invalid_indices.data(), invalid_indices.size()}),
            0u);
        auto error = ui->take_error();
        ASSERT_TRUE(error);
        EXPECT_NE(error->message.find("out-of-range index"), std::string::npos);
        const std::array<Rml::byte, 4> pixels{};
        EXPECT_EQ(interface.GenerateTexture({pixels.data(), pixels.size()}, {2, 1}), 0u);
        error = ui->take_error();
        ASSERT_TRUE(error);
        EXPECT_NE(error->message.find("texture data"), std::string::npos);
        interface.EnableClipMask(true);
        error = ui->take_error();
        ASSERT_TRUE(error);
        EXPECT_NE(error->message.find("clip masks"), std::string::npos);
        interface.PushLayer();
        error = ui->take_error();
        ASSERT_TRUE(error);
        EXPECT_NE(error->message.find("layers"), std::string::npos);
        EXPECT_FALSE(ui->take_error());
    }

    TEST_F(RmlUiGpuTest, PreservingSwapchainOverlaySurvivesResizeAndResourceRebuild) {
        auto& renderer = engine->get_renderer();
        unsigned draws = 0, releases = 0, rebuilds = 0;
        renderer.set_overlay({.render =
                                  [&](OverlayRecordContext& frame) {
                                      ++draws;
                                      return ui->render(frame, *context);
                                  },
            .release =
                [&] {
                    ++releases;
                    ui->release_swapchain_resources();
                },
            .rebuild =
                [&](const SwapchainCompatibility& compatibility) {
                    ++rebuilds;
                    return ui->rebuild_swapchain_resources(compatibility);
                }});
        ASSERT_TRUE(presentation_frame());
        const auto size = engine->get_window().get_framebuffer_size();
        glfwSetWindowSize(engine->get_window().get(), 240, 180);
        renderer.request_swapchain_recreation();
        ASSERT_TRUE(presentation_frame());
        EXPECT_NE(engine->get_window().get_framebuffer_size(), size);
        EXPECT_EQ(draws, 2u);
        EXPECT_GT(releases, 0u);
        EXPECT_EQ(releases, rebuilds);
    }

}
