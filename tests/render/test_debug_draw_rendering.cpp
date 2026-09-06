#include "core/engine.h"
#include "diagnostics/logger.h"
#include "render/line_draw_list.h"
#include "graphics/resource/image.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <sstream>
#include <tuple>

namespace Comet::Tests {
    class DebugDrawRenderingTest
        : public ::testing::TestWithParam<std::tuple<bool, SampleCount>> {
    protected:
        void SetUp() override {
            log_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(errors);
            log_sink->set_level(spdlog::level::err);
            Logger::add_custom_sink(log_sink);

            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            config.vulkan.msaa_samples = std::get<1>(GetParam());
            config.render.max_frames_in_flight = 2;
            engine = std::make_unique<Engine>(config);
            if(std::get<0>(GetParam())) {
                auto& renderer = engine->get_renderer();
                renderer.enable_offscreen_rendering({160, 120});
                auto& context = renderer.get_render_context();
                auto& swapchain = context.get_swapchain();
                const auto format = swapchain.get_images().front()->get_info().format;
                presentation_pass = std::make_unique<RenderPass>(context.get_device(),
                    std::vector{
                        Attachment::get_color_attachment(format, SampleCount::Count1)},
                    std::vector{RenderSubPass{{}, {SubpassColorAttachment(0)}, {}}},
                    format);
                presentation_target = RenderTarget::create_swapchain_target(
                    context.get_device(), *presentation_pass, swapchain);
                renderer.set_overlay_callbacks({},
                    [this](CommandBuffer& command_buffer) { present(command_buffer); });
            }
            scene.cameras.push_back(RenderCamera{.primary = true});
        }

        void TearDown() override {
            if(engine) {
                engine->get_renderer().get_render_context().wait_idle();
            }
            presentation_target.reset();
            presentation_pass.reset();
            engine.reset();
            if(auto logger = Logger::get_console_logger()) {
                std::erase(logger->sinks(), log_sink);
            }
            EXPECT_TRUE(errors.str().empty()) << errors.str();
        }

        struct Allocations {
            uint64_t count = 0;
            uint64_t bytes = 0;
        };

        void present(CommandBuffer& command_buffer) {
            // 离屏场景仍需最终交换链 pass，对应编辑器的 ImGui 呈现阶段。
            if(presentation_target) {
                presentation_target->begin_render_target(command_buffer);
                presentation_target->end_render_target(command_buffer);
            }
        }

        Allocations allocations() const {
            Allocations result;
            for(const auto& heap : engine->get_renderer()
                    .get_render_context()
                    .get_device()
                    .query_memory_budget()
                    .heaps) {
                result.count += heap.allocation_count;
                result.bytes += heap.allocation_bytes;
            }
            return result;
        }

        LineDrawList lines(const int count) const {
            LineDrawList list;
            for(int index = 0; index < count; ++index) {
                EXPECT_TRUE(list.add_line({-0.5f, 0, -2}, {0.5f, 0, -2}, {1, 0, 0, 1}));
            }
            return list;
        }

        bool draw_frame(const LineDrawList& list = {}) {
            auto& renderer = engine->get_renderer();
            engine->get_window().poll_events();
            renderer.submit_lines(list);
            if(!renderer.prepare_frame()) {
                return false;
            }
            renderer.render_frame(scene);
            return true;
        }

        std::ostringstream errors;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> log_sink;
        std::unique_ptr<Engine> engine;
        std::unique_ptr<RenderPass> presentation_pass;
        std::unique_ptr<RenderTarget> presentation_target;
        RenderScene scene;
    };

    TEST_P(DebugDrawRenderingTest, AppendsProducersConsumesOnceAndReusesSlotBuffers) {
        auto& renderer = engine->get_renderer();
        const auto initial = allocations();
        const auto batch = lines(100);
        renderer.set_overlay_callbacks([&] { renderer.submit_lines(batch); },
            [this](CommandBuffer& command_buffer) { present(command_buffer); });
        ASSERT_TRUE(draw_frame(batch));
        renderer.set_overlay_callbacks(
            {}, [this](CommandBuffer& command_buffer) { present(command_buffer); });
        const auto first_slot = allocations();
        EXPECT_EQ(first_slot.count, initial.count + 1);
        EXPECT_GE(first_slot.bytes - initial.bytes, batch.vertices().size_bytes() * 2);

        // 下帧换到另一 slot，不再提交时不能残留旧线段或创建第二个 buffer。
        ASSERT_TRUE(draw_frame());
        EXPECT_EQ(allocations().count, first_slot.count);
        ASSERT_TRUE(draw_frame(lines(600)));
        const auto grown = allocations();
        EXPECT_EQ(grown.count, first_slot.count);
        EXPECT_GT(grown.bytes, first_slot.bytes);

        ASSERT_TRUE(draw_frame(lines(600)));
        const auto both_slots = allocations();
        EXPECT_EQ(both_slots.count, initial.count + 2);
        for(int frame = 0; frame < 4; ++frame) {
            ASSERT_TRUE(draw_frame(batch));
            EXPECT_EQ(allocations().count, both_slots.count);
            EXPECT_EQ(allocations().bytes, both_slots.bytes);
        }

        if(std::get<0>(GetParam())) {
            renderer.set_render_view({.render_size = {192, 128}});
            ASSERT_TRUE(draw_frame(batch));
            EXPECT_EQ(renderer.get_scene_renderer().get_render_target().get_size(),
                Math::Vec2u(192, 128));
        }
    }

    TEST_P(DebugDrawRenderingTest, MissingCameraAndHiddenViewDiscardRequests) {
        auto& renderer = engine->get_renderer();
        const auto initial = allocations();
        scene.cameras.clear();
        ASSERT_TRUE(draw_frame(lines(100)));
        scene.cameras.push_back(RenderCamera{.primary = true});
        ASSERT_TRUE(draw_frame());
        EXPECT_EQ(allocations().count, initial.count);

        renderer.set_render_view({.visible = false});
        ASSERT_TRUE(draw_frame(lines(100)));
        renderer.set_render_view({});
        ASSERT_TRUE(draw_frame());
        EXPECT_EQ(allocations().count, initial.count);
    }

    INSTANTIATE_TEST_SUITE_P(SwapchainAndOffscreen, DebugDrawRenderingTest,
        ::testing::Combine(::testing::Bool(),
            ::testing::Values(SampleCount::Count1, SampleCount::Count4)));
}
