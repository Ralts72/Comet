#ifdef COMET_TEST_EDITOR_IMGUI
#include "ui/imgui_context.h"
#include "imgui_hdr_frag.h"
#include "support/render_gpu_test.h"
#include "common/scope_exit.h"
#include "render/render_thread.h"
#include "core/window.h"
#include "graphics/frame_buffer.h"
#include "graphics/swapchain.h"
#include "render/scene/render_submission.h"
#include "asset/data/texture_data.h"
#include "render/resource/texture.h"
#include "graphics/resource/sampler.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/packing.hpp>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include <array>
#include <cmath>
#include <cstring>
#include <atomic>
#include <future>
#include <latch>
#include <thread>

namespace Comet::Tests {
    namespace {
        // 原生后端的像素回读测试独立拥有上下文，不借用编辑器内部 GPU 状态。
        class NativeImGuiBackend {
        public:
            explicit NativeImGuiBackend(RenderContext& context)
                : m_render_context(context), m_previous(ImGui::GetCurrentContext()),
                  m_context(ImGui::CreateContext()) {
                ImGui::SetCurrentContext(m_context);
                ImGui::GetIO().IniFilename = nullptr;
            }
            ~NativeImGuiBackend() {
                m_render_context.get_device().wait_idle_for_shutdown();
                ImGui::SetCurrentContext(m_context);
                if(ImGui::GetIO().BackendRendererUserData)
                    ImGui_ImplVulkan_Shutdown();
                ImGui::DestroyContext(m_context);
                ImGui::SetCurrentContext(m_previous);
            }
            NativeImGuiBackend(const NativeImGuiBackend&) = delete;
            NativeImGuiBackend& operator=(const NativeImGuiBackend&) = delete;

            bool initialize(const RenderPass& pass, Math::Vec2u size, bool hdr = false) {
                auto& io = ImGui::GetIO();
                io.DisplaySize = {float(size.x), float(size.y)};
                io.DeltaTime = 1.0f / 60;
                auto& context = m_render_context.get_context();
                auto& device = m_render_context.get_device();
                ImGui_ImplVulkan_InitInfo info{};
                info.ApiVersion = VK_API_VERSION_1_0;
                info.Instance = context.instance();
                info.PhysicalDevice = context.get_physical_device();
                info.Device = device.get();
                info.QueueFamily = context.get_graphics_queue_family().queue_family_index.value();
                info.Queue = device.get_graphics_queue().get();
                info.DescriptorPoolSize = 16;
                info.MinImageCount = info.ImageCount = 2;
                info.PipelineInfoMain.RenderPass = pass.get();
                info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
                if(hdr) {
                    auto& shader = info.CustomShaderFragCreateInfo;
                    shader.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                    shader.codeSize = IMGUI_HDR_FRAG.size() * sizeof(std::uint32_t);
                    shader.pCode = IMGUI_HDR_FRAG.data();
                }
                return ImGui_ImplVulkan_Init(&info);
            }

        private:
            RenderContext& m_render_context;
            ::ImGuiContext* m_previous;
            ::ImGuiContext* m_context;
        };

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

    TEST(ImGuiContextTest, CurrentContextIsLocalToEachThread) {
        auto* previous = ImGui::GetCurrentContext();
        auto* frontend = ImGui::CreateContext();
        ImGui::SetCurrentContext(frontend);
        const ScopeExit restore([&] {
            ImGui::DestroyContext(frontend);
            ImGui::SetCurrentContext(previous);
        });
        std::latch selected{1};
        std::latch release{1};
        std::jthread worker([&] {
            EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
            auto* backend = ImGui::CreateContext();
            ImGui::SetCurrentContext(backend);
            ImGui::GetStyle().Alpha = 0.25f;
            selected.count_down();
            release.wait();
            EXPECT_EQ(ImGui::GetCurrentContext(), backend);
            EXPECT_FLOAT_EQ(ImGui::GetStyle().Alpha, 0.25f);
            ImGui::DestroyContext(backend);
            EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
        });
        selected.wait();
        EXPECT_EQ(ImGui::GetCurrentContext(), frontend);
        EXPECT_FLOAT_EQ(ImGui::GetStyle().Alpha, 1.0f);
        release.count_down();
        worker.join();
        EXPECT_EQ(ImGui::GetCurrentContext(), frontend);
    }

    class ImGuiOverlayGpuTest: public RenderGpuTest {
    protected:
        std::unique_ptr<CometEditor::Ui::ImGuiContext> ui;
        std::unique_ptr<CometEditor::Ui::ImGuiContext::Frame> ui_frame;

        void create_ui() {
            auto created = CometEditor::Ui::ImGuiContext::create(engine->get_window(),
                engine->get_renderer().get_render_context(),
                {.composition = CometEditor::Ui::ImGuiContext::Composition::Preserve});
            ASSERT_TRUE(created) << created.error();
            ui = std::move(created).value();
        }

        void TearDown() override {
            if(engine) {
                engine->get_renderer().set_overlay({});
                engine->get_renderer().wait_idle();
            }
            ui_frame.reset();
            ui.reset();
            RenderGpuTest::TearDown();
        }

        Result<void, GraphicsError> presentation_frame(bool draw_window) {
            auto& renderer = engine->get_renderer();
            for(unsigned attempt = 0; attempt < 12; ++attempt) {
                engine->get_window().poll_events();
                auto prepared = render_frontend->prepare_frame();
                if(!prepared)
                    return Result<void, GraphicsError>::failure(prepared.error());
                if(prepared.value() != Renderer::FramePreparation::Ready)
                    continue;
                if(ui) {
                    if(!ui->begin_frame())
                        return Result<void, GraphicsError>::failure({"UI frame unavailable"});
                    if(draw_window) {
                        EXPECT_STREQ(ImGui::GetFont()->GetDebugName(), "Roboto-Bold.ttf");
                        EXPECT_FLOAT_EQ(ImGui::GetStyle().FontSizeBase, 16.0f);
                        EXPECT_GT(ImGui::GetFontBaked()->GetCharAdvance('W'),
                            ImGui::GetFontBaked()->GetCharAdvance('i'));
                        for(const auto glyph : U"玩家改键")
                            if(glyph != 0)
                                EXPECT_TRUE(
                                    ImGui::GetFont()->IsGlyphInFont(static_cast<ImWchar>(glyph)));
                        transparent_window();
                    }
                    ui_frame = ui->end_frame();
                }
                RenderScene scene;
                scene.environment.background_color = {0.3f, 0.1f, 0.05f};
                scene.cameras.push_back(RenderCamera{.primary = true});
                return render_frontend->render(scene);
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
        Comet::Tests::set_test_overlay(renderer,
            {.render =
                    [&](Comet::OverlayRecordContext& overlay) {
                        ++rendered;
                        return Comet::Tests::record_draw_frame(overlay, std::move(ui_frame));
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

    TEST_F(ImGuiOverlayGpuTest, CursorLockDisablesUiMouseWithoutDisablingKeyboard) {
        create_ui();
        ASSERT_TRUE(ui);
        auto& window = engine->get_window();
        window.poll_events();
        if(glfwGetWindowAttrib(window.get(), GLFW_FOCUSED) != GLFW_TRUE)
            GTEST_SKIP() << "Cursor locking requires a focused native window";
        const ScopeExit release_cursor([&] { window.set_cursor_locked(false); });
        glfwSetCursorPos(window.get(), 50, 50);
        const auto enter = glfwSetCursorEnterCallback(window.get(), nullptr);
        glfwSetCursorEnterCallback(window.get(), enter);
        ASSERT_NE(enter, nullptr);
        enter(window.get(), GLFW_TRUE);
        const auto draw_window = [] {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({100, 100});
            ImGui::Begin("Input target");
            ImGui::TextUnformatted("Test");
            ImGui::End();
        };
        ASSERT_TRUE(ui->begin_frame());
        draw_window();
        ui->end_frame();
        const auto* focused = ImGui::GetCurrentContext()->NavWindow;
        ASSERT_NE(focused, nullptr);
        // 左键绑定转向时，初始按下仍经过正常 UI 帧的窗口移动处理。
        ImGui::GetIO().AddMousePosEvent(50, 70);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        ASSERT_TRUE(ui->begin_frame());
        EXPECT_TRUE(ImGui::IsMouseClicked(ImGuiMouseButton_Left));
        draw_window();
        ui->end_frame();
        EXPECT_EQ(ImGui::GetCurrentContext()->ActiveId, focused->MoveId);
        const auto window_position = focused->Pos;
        window.set_cursor_locked(true);
        ASSERT_TRUE(window.is_cursor_locked());
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Space, true);
        ImGui::GetIO().AddMousePosEvent(50, 50);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        for(int frame = 0; frame < 3; ++frame) {
            ASSERT_TRUE(ui->begin_frame());
            EXPECT_TRUE(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse);
            EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_Space));
            EXPECT_FALSE(ImGui::IsMouseDown(ImGuiMouseButton_Left));
            EXPECT_FALSE(ImGui::GetIO().WantCaptureMouse);
            EXPECT_TRUE(window.is_cursor_locked());
            draw_window();
            ui->end_frame();
            EXPECT_EQ(ImGui::GetCurrentContext()->NavWindow, focused);
            EXPECT_EQ(focused->Pos.x, window_position.x);
            EXPECT_EQ(focused->Pos.y, window_position.y);
            EXPECT_EQ(ImGui::GetCurrentContext()->ActiveId, 0u);
        }
        window.set_cursor_locked(false);
        // 模拟静止恢复时没有额外的位置回调。
        ImGui::GetIO().ClearEventsQueue();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        ASSERT_TRUE(ui->begin_frame());
        EXPECT_FALSE(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse);
        EXPECT_EQ(ImGui::GetIO().MousePos.x, window.get_cursor_position().x);
        EXPECT_EQ(ImGui::GetIO().MousePos.y, window.get_cursor_position().y);
        EXPECT_TRUE(ImGui::GetIO().WantCaptureMouse);
        EXPECT_TRUE(ImGui::IsMouseClicked(ImGuiMouseButton_Left));
        draw_window();
        ui->end_frame();
    }

    TEST_F(ImGuiOverlayGpuTest, CapturedFrameOwnsFontsAndCallbackDataAcrossLaterUiFrames) {
        create_ui();
        ASSERT_TRUE(ui);
        auto& renderer = engine->get_renderer();
        const auto prepared = render_frontend->prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        std::atomic<int> recorded = 0;
        struct Payload {
            std::atomic<int>* output;
            int value;
            ::ImGuiContext* frontend;
        };
        const auto draw = [&](const int value) {
            Payload payload{&recorded, value, ImGui::GetCurrentContext()};
            ImGui::SetNextWindowPos({16, 16});
            ImGui::SetNextWindowSize({120, 100});
            ImGui::Begin("Captured UI");
            ImGui::TextUnformatted("独立字体快照");
            ImGui::GetWindowDrawList()->AddCallback(
                [](const ImDrawList* list, const ImDrawCmd* command) {
                    const auto& owned = *static_cast<const Payload*>(command->UserCallbackData);
                    EXPECT_GT(list->VtxBuffer.Size, 0);
                    EXPECT_NE(ImGui::GetCurrentContext(), owned.frontend);
                    EXPECT_NE(ImGui::GetIO().BackendRendererUserData, nullptr);
                    EXPECT_EQ(ImGui::GetIO().BackendPlatformUserData, nullptr);
                    EXPECT_NE(ImGui::GetMainViewport()->RendererUserData, nullptr);
                    owned.output->store(owned.value);
                },
                &payload, sizeof(payload));
            ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
            ImGui::End();
        };
        ASSERT_TRUE(ui->begin_frame());
        draw(41);
        ui_frame = ui->end_frame();
        // 删除源字体并复用同一窗口的绘制与回调存储，旧帧仍有自己的数据。
        ImGui::GetIO().Fonts->Clear();
        ASSERT_TRUE(ui->begin_frame());
        draw(99);
        auto newer = ui->end_frame();
        newer.reset();
        auto* original = ImGui::GetCurrentContext();
        auto* other = ImGui::CreateContext();
        ImGui::SetCurrentContext(other);
        const ScopeExit restore_context([&] {
            ImGui::DestroyContext(other);
            ImGui::SetCurrentContext(original);
        });
        Comet::Tests::set_test_overlay(renderer, {.render = [&](OverlayRecordContext& overlay) {
            return Comet::Tests::record_draw_frame(overlay, std::move(ui_frame));
        }});
        ASSERT_TRUE(renderer.render_frame({}));
        EXPECT_EQ(recorded.load(), 41);
        EXPECT_EQ(ImGui::GetCurrentContext(), other);
        EXPECT_FALSE(ui_frame);
    }

    TEST_F(ImGuiOverlayGpuTest, MainBuildsANewUiFrameWhileWorkerRecordsCapturedInputs) {
        create_ui();
        ASSERT_TRUE(ui);
        auto& renderer = engine->get_renderer();
        const auto prepared = render_frontend->prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        RenderScene scene;
        scene.cameras.push_back(RenderCamera{.primary = true});
        auto world = render_frontend->resolve(scene);
        ASSERT_TRUE(world);
        std::promise<void> entered;
        auto recording_started = entered.get_future();
        std::latch release{1};
        struct CallbackInput {
            std::promise<void>* entered;
            std::latch* release;
            ::ImGuiContext* frontend;
        };
        ASSERT_TRUE(ui->begin_frame());
        auto* frontend = ImGui::GetCurrentContext();
        CallbackInput input{&entered, &release, frontend};
        ImGui::SetNextWindowPos({16, 16});
        ImGui::SetNextWindowSize({120, 100});
        ImGui::Begin("Captured worker frame");
        ImGui::TextUnformatted("固定的 UI 输入");
        ImGui::GetWindowDrawList()->AddCallback(
            [](const ImDrawList*, const ImDrawCmd* command) {
                const auto& input = *static_cast<const CallbackInput*>(command->UserCallbackData);
                EXPECT_NE(ImGui::GetCurrentContext(), input.frontend);
                EXPECT_NE(ImGui::GetIO().BackendRendererUserData, nullptr);
                EXPECT_EQ(ImGui::GetIO().BackendPlatformUserData, nullptr);
                input.entered->set_value();
                input.release->wait();
            },
            &input, sizeof(input));
        ImGui::End();
        ui_frame = ui->end_frame();
        renderer.set_overlay({.capture = [&] {
            OverlayDrawList draws;
            draws.push_back(std::move(ui_frame));
            return draws;
        }});
        RenderThread recording(renderer);
        auto captured = renderer.capture_frame();
        captured.world = std::move(world);
        ASSERT_TRUE(recording.submit(std::move(captured)));
        ScopeExit unblock([&] { release.count_down(); });
        const auto status = recording_started.wait_for(std::chrono::seconds(5));
        EXPECT_EQ(status, std::future_status::ready);
        EXPECT_EQ(ImGui::GetCurrentContext(), frontend);
        if(status != std::future_status::ready || ImGui::GetCurrentContext() != frontend)
            return;
        EXPECT_FALSE(recording.submit({}));
        ASSERT_TRUE(ui->begin_frame());
        ImGui::Begin("New frontend frame");
        ImGui::TextUnformatted("录制期间构建下一帧并添加新字形：渲染线程");
        ImGui::End();
        auto newer = ui->end_frame();
        newer.reset();
        release.count_down();
        unblock.release();
        const auto result = recording.complete();
        ASSERT_TRUE(result) << result.error();
        EXPECT_FALSE(ui_frame);
        EXPECT_EQ(ImGui::GetCurrentContext(), frontend);
    }

    TEST_F(ImGuiOverlayGpuTest, HostBuildsUiOnMainAndWorkerDrawsAcrossSwapchainRecovery) {
        create_ui();
        ASSERT_TRUE(ui);
        auto& renderer = engine->get_renderer();
        const auto main_thread = std::this_thread::get_id();
        unsigned ready = 0;
        unsigned draws = 0;
        unsigned rebuilds = 0;
        unsigned shutdowns = 0;
        renderer.set_overlay({.capture =
                                  [&] {
                                      EXPECT_EQ(std::this_thread::get_id(), main_thread);
                                      OverlayDrawList captured;
                                      captured.push_back(std::move(ui_frame));
                                      return captured;
                                  },
            .release =
                [&] {
                    EXPECT_NE(std::this_thread::get_id(), main_thread);
                    ui->release_swapchain_resources();
                },
            .rebuild =
                [&](const SwapchainCompatibility& compatibility) {
                    EXPECT_NE(std::this_thread::get_id(), main_thread);
                    ++rebuilds;
                    return ui->rebuild_swapchain_resources(compatibility);
                },
            .shutdown =
                [&] {
                    EXPECT_NE(std::this_thread::get_id(), main_thread);
                    ++shutdowns;
                    ui->shutdown_gpu();
                }});
        const auto result = engine->run({.frame_ready = [&](const Engine::FrameContext&) {
            EXPECT_EQ(std::this_thread::get_id(), main_thread);
            EXPECT_EQ(draws, ready);
            ++ready;
            if(ready == 2) {
                glfwSetWindowSize(engine->get_window().get(), 240, 180);
                renderer.request_swapchain_recreation();
            }
            if(!ui->begin_frame())
                return Result<void, Error>::failure({"UI frame unavailable"});
            transparent_window();
            struct Observation {
                std::thread::id main;
                unsigned* draws;
            };
            Observation observation{main_thread, &draws};
            ImGui::GetForegroundDrawList()->AddCallback(
                [](const ImDrawList*, const ImDrawCmd* command) {
                    const auto& observation =
                        *static_cast<const Observation*>(command->UserCallbackData);
                    EXPECT_NE(std::this_thread::get_id(), observation.main);
                    ++*observation.draws;
                },
                &observation, sizeof(observation));
            ui_frame = ui->end_frame();
            if(ready == 4)
                engine->get_window().request_close();
            return Result<void, Error>::success();
        }});
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(draws, 4u);
        EXPECT_GT(rebuilds, 0u);
        EXPECT_FALSE(ui_frame);
        auto* frontend = ImGui::GetCurrentContext();
        engine->prepare_shutdown();
        EXPECT_EQ(shutdowns, 1u);
        EXPECT_EQ(ImGui::GetCurrentContext(), frontend);
        EXPECT_NE(ImGui::GetIO().BackendPlatformUserData, nullptr);
    }

    TEST_F(ImGuiOverlayGpuTest, SwapchainRebuildDiscardsOldFramesAndAcceptsNewFrames) {
        create_ui();
        ASSERT_TRUE(ui);
        auto& renderer = engine->get_renderer();
        unsigned callbacks = 0;
        ASSERT_TRUE(ui->begin_frame());
        ImGui::SetNextWindowPos({16, 16});
        ImGui::SetNextWindowSize({120, 100});
        ImGui::Begin("Old backend");
        ImGui::TextUnformatted("旧帧");
        auto* output = &callbacks;
        ImGui::GetWindowDrawList()->AddCallback(
            [](const ImDrawList*, const ImDrawCmd* command) {
                ++**static_cast<unsigned* const*>(command->UserCallbackData);
            },
            &output, sizeof(output));
        ImGui::End();
        ui_frame = ui->end_frame();
        renderer.wait_idle();
        ui->release_swapchain_resources();
        ASSERT_TRUE(ui->rebuild_swapchain_resources({.image_count_changed = true}));
        const auto prepared = render_frontend->prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        Comet::Tests::set_test_overlay(renderer, {.render = [&](OverlayRecordContext& overlay) {
            return Comet::Tests::record_draw_frame(overlay, std::move(ui_frame));
        }});
        ASSERT_TRUE(renderer.render_frame({}));
        EXPECT_EQ(callbacks, 0);
        ASSERT_TRUE(presentation_frame(true));
        EXPECT_FALSE(ui_frame);
        ASSERT_TRUE(ui->begin_frame());
        transparent_window();
        auto unrecorded = ui->end_frame();
        renderer.set_overlay({});
        renderer.wait_idle();
        ui.reset();
        // 尚未录制的 CPU 帧也可在关闭前端后正常释放。
        unrecorded.reset();
    }

    TEST_F(ImGuiOverlayGpuTest, CapturedViewportPinsImageVersionsAndReusesUnchangedBindings) {
        create_ui();
        ASSERT_TRUE(ui);
        auto& renderer = engine->get_renderer();
        auto& resources = engine->get_render_resources();
        auto old_result =
            resources.try_create_texture({.width = 1, .height = 1, .pixels = {255, 0, 0, 255}});
        auto next_result =
            resources.try_create_texture({.width = 1, .height = 1, .pixels = {0, 0, 255, 255}});
        auto sampler_result = resources.get_sampler_manager().get_nearest_clamp();
        ASSERT_TRUE(old_result) << old_result.error();
        ASSERT_TRUE(next_result) << next_result.error();
        ASSERT_TRUE(sampler_result) << sampler_result.error();
        auto old = std::move(old_result).value();
        auto next = std::move(next_result).value();
        auto sampler = std::move(sampler_result).value();
        const auto old_ready = old->get_ready_completion();
        const std::weak_ptr<ImageView> old_image = old->get_image_view();
        ImTextureID descriptor = ImTextureID_Invalid;
        struct Observation {
            ImTextureID logical;
            ImTextureID* descriptor;
        };
        const auto capture = [&](ImTextureID logical) {
            EXPECT_TRUE(ui->begin_frame());
            auto* list = ImGui::GetBackgroundDrawList();
            list->AddImage(logical, {8, 8}, {100, 80});
            Observation observation{logical, &descriptor};
            list->AddCallback(
                [](const ImDrawList* recorded, const ImDrawCmd* callback) {
                    const auto& observed =
                        *static_cast<const Observation*>(callback->UserCallbackData);
                    ASSERT_GT(recorded->CmdBuffer.Size, 0);
                    const auto actual = recorded->CmdBuffer.front().GetTexID();
                    EXPECT_NE(actual, ImTextureID_Invalid);
                    EXPECT_NE(actual, observed.logical);
                    *observed.descriptor = actual;
                },
                &observation, sizeof(observation));
            return ui->end_frame();
        };
        const auto prepared = render_frontend->prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        const auto slot = renderer.get_frame_scheduler().get_current_frame_slot_index();
        ui->set_viewport_image(slot, old->get_image_view(), sampler);
        const auto logical = ui->get_viewport_texture_id(slot);
        ASSERT_NE(logical, ImTextureID_Invalid);
        ui_frame = capture(logical);
        ui->set_viewport_image(slot, next->get_image_view(), sampler);
        EXPECT_EQ(ui->get_viewport_texture_id(slot), logical);
        auto newer = capture(logical);
        newer.reset();
        old.reset();
        EXPECT_FALSE(old_image.expired());
        Comet::Tests::set_test_overlay(renderer, {.render = [&](OverlayRecordContext& overlay) {
            overlay.wait_for(old_ready, Flags<PipelineStage>(PipelineStage::FragmentShader));
            overlay.wait_for(
                next->get_ready_completion(), Flags<PipelineStage>(PipelineStage::FragmentShader));
            return Comet::Tests::record_draw_frame(overlay, std::move(ui_frame));
        }});
        ASSERT_TRUE(renderer.render_frame({}));
        EXPECT_NE(descriptor, ImTextureID_Invalid);
        // CPU 帧已消费；旧图片仍由录制使用的 GPU 绑定保活。
        EXPECT_FALSE(old_image.expired());
        std::map<ImTextureID, ImTextureID> descriptors;
        unsigned reused = 0;
        const auto frame_count = renderer.get_frame_scheduler().get_frame_slot_count() * 2;
        for(unsigned index = 0; index < frame_count; ++index) {
            const auto ready = render_frontend->prepare_frame();
            ASSERT_TRUE(ready) << ready.error();
            ASSERT_EQ(ready.value(), Renderer::FramePreparation::Ready);
            const auto frame_slot = renderer.get_frame_scheduler().get_current_frame_slot_index();
            ui->set_viewport_image(frame_slot, next->get_image_view(), sampler);
            const auto id = ui->get_viewport_texture_id(frame_slot);
            ui_frame = capture(id);
            descriptor = ImTextureID_Invalid;
            ASSERT_TRUE(renderer.render_frame({}));
            ASSERT_NE(descriptor, ImTextureID_Invalid);
            const auto [found, inserted] = descriptors.emplace(id, descriptor);
            if(!inserted) {
                ++reused;
                EXPECT_EQ(found->second, descriptor);
            }
        }
        renderer.wait_idle();
        EXPECT_TRUE(old_image.expired());
        EXPECT_EQ(reused, renderer.get_frame_scheduler().get_frame_slot_count());
    }

    TEST_F(ImGuiOverlayGpuTest, ViewportPacketRejectsImagesFromAnotherFrameSlot) {
        create_ui();
        ASSERT_TRUE(ui);
        auto& renderer = engine->get_renderer();
        auto texture = engine->get_render_resources().try_create_texture(
            {.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        auto sampler = engine->get_render_resources().get_sampler_manager().get_nearest_clamp();
        ASSERT_TRUE(texture) << texture.error();
        ASSERT_TRUE(sampler) << sampler.error();
        const auto prepared = render_frontend->prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        const auto slot = renderer.get_frame_scheduler().get_current_frame_slot_index() + 1;
        ui->set_viewport_image(slot, texture.value()->get_image_view(), sampler.value());
        ASSERT_TRUE(ui->begin_frame());
        ImGui::GetBackgroundDrawList()->AddImage(
            ui->get_viewport_texture_id(slot), {8, 8}, {100, 80});
        ui_frame = ui->end_frame();
        Comet::Tests::set_test_overlay(renderer, {.render = [&](OverlayRecordContext& overlay) {
            const auto recorded = Comet::Tests::record_draw_frame(overlay, std::move(ui_frame));
            EXPECT_FALSE(recorded);
            if(!recorded)
                EXPECT_NE(recorded.error().message.find("another frame slot"), std::string::npos);
            return Result<void, GraphicsError>::success();
        }});
        ASSERT_TRUE(renderer.render_frame({}));
        EXPECT_FALSE(ui_frame);
    }

    TEST_F(ImGuiOverlayGpuTest, CompatibleOffscreenBackendKeepsEmptyUiAndPixelsOutsideWindow) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        const auto size = engine->get_window().get_framebuffer_size();
        const auto format = context.get_swapchain().get_images().front()->get_info().format;
        Config config;
        config.vulkan.surface_format = format;
        config.vulkan.msaa_samples = SampleCount::Count1;
        MaterialPrograms programs;
        auto scene_owner = SceneRenderer::create(
            device, programs, engine->get_render_resources(), config.vulkan, config.render, size);
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        ASSERT_EQ(scene.get_offscreen_color_view(0)->get_image()->get_info().format, format);
        auto overlay_owner = load_pass(device, format);
        ASSERT_TRUE(overlay_owner) << overlay_owner.error();
        std::shared_ptr<RenderPass> overlay = std::move(overlay_owner).value();
        NativeImGuiBackend backend(context);
        ASSERT_TRUE(backend.initialize(*overlay, size));
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        RenderWorld world;
        RenderSubmission submission{world};
        world.environment.background_color = {0.2f, 0.4f, 0.8f};
        std::array<std::shared_ptr<Readback>, 3> outputs;
        Math::Vec2u inside{};
        for(std::size_t index = 0; index < outputs.size(); ++index) {
            ImGui_ImplVulkan_NewFrame();
            ImGui::NewFrame();
            if(index == 2)
                transparent_window();
            const auto scale = ImGui::GetIO().DisplayFramebufferScale;
            inside = {static_cast<unsigned>(40 * scale.x), static_cast<unsigned>(40 * scale.y)};
            // 此例直接验证第三方后端的离屏混合；生产快照路径另有交接回归。
            ImGui::Render();
            if(index == 1)
                EXPECT_EQ(ImGui::GetDrawData()->CmdListsCount, 0);
            if(index == 2)
                EXPECT_GT(ImGui::GetDrawData()->CmdListsCount, 0);
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            const auto slot = frames.get_current_frame_slot_index();
            const auto image = scene.get_offscreen_color_view(slot)->get_image();
            RenderGeometry geometry;
            geometry.prepare(world);
            const auto drawn = scene.render(frames, submission, geometry);
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

        NativeImGuiBackend backend(context);
        ASSERT_TRUE(backend.initialize(*overlay.value(), size, true));
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
