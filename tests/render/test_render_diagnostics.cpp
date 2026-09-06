#include "render/gpu_test.h"
#include "render/render_diagnostics.h"

#include <limits>

#ifdef COMET_TEST_EDITOR_UI
#include "panels/render_stats.h"
#include <imgui.h>
#include <imgui_internal.h>
#endif

namespace Comet::Tests {
    using RenderDiagnosticsGpuTest = RenderGpuTest;

    TEST(RenderDiagnosticsTest,
        ConvertsTimestampPeriodAndCounterWrapWithoutUndefinedShift) {
        EXPECT_DOUBLE_EQ(*RenderDiagnostics::elapsed_ms(100, 1100, 64, 2), 0.002);
        EXPECT_DOUBLE_EQ(*RenderDiagnostics::elapsed_ms(250, 5, 8, 1000), 0.011);
        EXPECT_DOUBLE_EQ(*RenderDiagnostics::elapsed_ms(
                             std::numeric_limits<uint64_t>::max() - 4, 3, 64, 1000),
            0.008);
        EXPECT_DOUBLE_EQ(*RenderDiagnostics::elapsed_ms(99, 99, 36, 1), 0);
        for(const auto bits : {0u, 65u})
            EXPECT_FALSE(RenderDiagnostics::elapsed_ms(0, 1, bits, 1));
        for(const auto period : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()})
            EXPECT_FALSE(RenderDiagnostics::elapsed_ms(0, 1, 64, period));
    }

    TEST_F(RenderDiagnosticsGpuTest,
        ReadsOnlyCompletedSubmissionsAndNeverOldAvailableValues) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        RenderDiagnostics diagnostics(device, frames);
        diagnostics.set_enabled(true);
        if(!diagnostics.get_snapshot().gpu_supported)
            GTEST_SKIP()
                << "Graphics queue timestamps unavailable; CPU fallback is tested separately";
        auto buffer = Buffer::create_gpu_buffer(
            device, Flags<BufferUsage>(BufferUsage::CopyDst), 1024 * 1024);
        for(uint64_t index = 0; index < 5; ++index) {
            const auto pass_count = index % 2 == 0 ? 2u : 3u;
            RenderGraph graph;
            const auto data = graph.import_buffer("data", {{}, 0, buffer->get_size()});
            for(unsigned pass = 0; pass < pass_count; ++pass)
                graph.add_pass(
                    {"frame " + std::to_string(index) + " pass " + std::to_string(pass),
                        {{data, ResourceUsage::TransferDestination, {}}}});
            frames.wait_for_current_slot();
            frames.begin_frame(static_cast<uint32_t>(index % 2));
            frames.get_current_command_buffer().begin();
            const std::vector<RenderGraph::Binding> bindings{buffer};
            diagnostics.record(graph.compile(), bindings,
                [&](size_t pass, const CommandBuffer& command) {
                    command.get().fillBuffer(buffer->get(), 0, buffer->get_size(),
                        static_cast<uint32_t>(pass));
                });
            ASSERT_TRUE(diagnostics.get_snapshot().cpu);
            EXPECT_EQ(diagnostics.get_snapshot().cpu->serial, index + 1);
            EXPECT_EQ(diagnostics.get_snapshot().cpu->passes.size(), pass_count);
            diagnostics.collect_completed();
            if(index == 0 || !diagnostics.get_snapshot().gpu_supported)
                EXPECT_FALSE(diagnostics.get_snapshot().gpu);
            else
                EXPECT_EQ(diagnostics.get_snapshot().gpu->serial, index);
            submit(device, frames);
            // 没有新的 fence 完成确认时，即便 GPU 实际已很快完成，也不能读本轮查询。
            diagnostics.collect_completed();
            if(index > 0 && diagnostics.get_snapshot().gpu_supported)
                EXPECT_EQ(diagnostics.get_snapshot().gpu->serial, index);
            frames.wait_for_all_slots();
            diagnostics.collect_completed();
            if(diagnostics.get_snapshot().gpu_supported) {
                ASSERT_TRUE(diagnostics.get_snapshot().gpu)
                    << diagnostics.get_snapshot().gpu_error;
                const auto& timing = *diagnostics.get_snapshot().gpu;
                EXPECT_EQ(timing.serial, index + 1);
                ASSERT_EQ(timing.passes.size(), pass_count);
                EXPECT_EQ(
                    timing.passes[0].name, "frame " + std::to_string(index) + " pass 0");
                EXPECT_GE(timing.milliseconds, 0);
                double sum = 0;
                for(const auto& pass : timing.passes) {
                    EXPECT_GE(pass.milliseconds, 0);
                    sum += pass.milliseconds;
                }
                EXPECT_NEAR(sum, timing.milliseconds, 1e-6);
                EXPECT_TRUE(diagnostics.get_snapshot().gpu_error.empty());
                RecordProperty("last_gpu_ms", std::to_string(timing.milliseconds));
            }
        }
    }

    TEST_F(RenderDiagnosticsGpuTest,
        CpuFallbackBoundsPassDetailAndRecoversFromCallbackFailure) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RenderDiagnostics diagnostics(device, frames, false);
        EXPECT_FALSE(diagnostics.get_snapshot().gpu_supported);
        diagnostics.set_enabled(true);
        RenderGraph graph;
        for(unsigned index = 0; index < RenderDiagnostics::MAX_PASSES + 3; ++index)
            graph.add_pass({"pass " + std::to_string(index), {}});
        const auto plan = graph.compile();
        EXPECT_THROW(diagnostics.record(plan, {}, [](size_t, const CommandBuffer&) {}),
            std::invalid_argument);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        EXPECT_THROW(diagnostics.record(plan, {},
                         [](size_t, const CommandBuffer&) {
                             throw std::runtime_error("injected callback failure");
                         }),
            std::runtime_error);
        EXPECT_FALSE(diagnostics.get_snapshot().cpu);
        EXPECT_NO_THROW(diagnostics.set_enabled(true));
        unsigned calls = 0;
        diagnostics.record(plan, {}, [&](size_t, const CommandBuffer&) {
            ++calls;
            EXPECT_THROW(diagnostics.set_enabled(false), std::logic_error);
            EXPECT_THROW(diagnostics.collect_completed(), std::logic_error);
        });
        EXPECT_EQ(calls, RenderDiagnostics::MAX_PASSES + 3);
        ASSERT_TRUE(diagnostics.get_snapshot().cpu);
        EXPECT_TRUE(diagnostics.get_snapshot().cpu->truncated);
        EXPECT_EQ(
            diagnostics.get_snapshot().cpu->passes.size(), RenderDiagnostics::MAX_PASSES);
        submit(device, frames);
        frames.wait_for_all_slots();
        diagnostics.collect_completed();
        EXPECT_FALSE(diagnostics.get_snapshot().gpu);
        diagnostics.set_enabled(false);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        diagnostics.record(plan, {}, [&](size_t, const CommandBuffer&) {
            ++calls;
            EXPECT_THROW(diagnostics.set_enabled(true), std::logic_error);
        });
        EXPECT_EQ(calls, 2 * (RenderDiagnostics::MAX_PASSES + 3));
        EXPECT_EQ(diagnostics.get_snapshot().cpu->serial, 1);
        submit(device, frames);
    }

    TEST_F(RenderDiagnosticsGpuTest,
        QueryOwnersSurviveDiagnosticsDestructionBeforeSubmission) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RenderGraph graph;
        graph.add_pass({"measured", {}});
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        {
            RenderDiagnostics diagnostics(device, frames);
            diagnostics.set_enabled(true);
            diagnostics.record(graph.compile(), {}, [](size_t, const CommandBuffer&) {});
        }
        submit(device, frames);
        frames.wait_for_all_slots();
    }

    TEST_F(RenderDiagnosticsGpuTest,
        MemorySamplingIsLowFrequencyAndAllocationReportIsExplicit) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 1);
        RenderDiagnostics diagnostics(device, frames, false);
        const auto start =
            RenderDiagnostics::Clock::time_point{} + std::chrono::seconds(100);
        diagnostics.poll_memory(start);
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 0);
        diagnostics.set_enabled(true);
        diagnostics.poll_memory(start);
        for(unsigned index = 1; index < 1000; ++index)
            diagnostics.poll_memory(start + std::chrono::milliseconds(index));
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 1);
        auto allocation = Buffer::create_gpu_buffer(device,
            Flags<BufferUsage>(BufferUsage::CopyDst), 4096, "diagnostics-report-test");
        diagnostics.poll_memory(start + std::chrono::seconds(1));
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 2);
        EXPECT_FALSE(diagnostics.get_snapshot().memory.heaps.empty());
        diagnostics.set_enabled(false);
        diagnostics.poll_memory(start + std::chrono::seconds(10));
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 2);
        const auto report = device.build_allocation_report();
        EXPECT_NE(report.find("\"Total\""), std::string::npos);
        EXPECT_NE(report.find("\"AllocationCount\""), std::string::npos);
        EXPECT_NE(report.find("diagnostics-report-test"), std::string::npos);
        allocation.reset();
        EXPECT_EQ(device.build_allocation_report().find("diagnostics-report-test"),
            std::string::npos);
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 2);
    }

    TEST_F(
        RenderDiagnosticsGpuTest, EngineReportsCpuWallPhasesAndActualSceneGraphTimings) {
        auto& renderer = engine->get_renderer();
        auto& diagnostics = renderer.get_scene_renderer().get_diagnostics();
        diagnostics.set_enabled(true);
        unsigned updates = 0;
        engine->register_update_callback([&](UpdateContext) {
            EXPECT_EQ(engine->get_input_frame().serial, updates + 1);
            if(++updates == 3)
                glfwSetWindowShouldClose(engine->get_window().get(), GLFW_TRUE);
        });
        engine->on_update();
        ASSERT_EQ(updates, 3);
        ASSERT_TRUE(engine->get_frame_timing());
        const auto& timing = *engine->get_frame_timing();
        EXPECT_TRUE(timing.rendered);
        EXPECT_GE(timing.events_ms, 0);
        EXPECT_GE(timing.update_ms, 0);
        EXPECT_GE(timing.prepare_ms, 0);
        EXPECT_GE(timing.render_submit_ms, 0);
        EXPECT_NEAR(timing.events_ms + timing.update_ms + timing.prepare_ms
                        + timing.render_submit_ms,
            timing.total_ms, 1e-6);
        renderer.get_scene_renderer().get_frame_scheduler().wait_for_all_slots();
        diagnostics.collect_completed();
        ASSERT_TRUE(diagnostics.get_snapshot().cpu);
        EXPECT_EQ(diagnostics.get_snapshot().cpu->passes.size(), 3);
        if(diagnostics.get_snapshot().gpu_supported) {
            ASSERT_TRUE(diagnostics.get_snapshot().gpu);
            EXPECT_EQ(diagnostics.get_snapshot().gpu->passes.size(), 3);
        }
    }

#ifdef COMET_TEST_EDITOR_UI
    TEST_F(
        RenderDiagnosticsGpuTest, StatsPanelRequestsCaptureAndReportOnlyOnInteraction) {
        struct ImGuiLifetime {
            ImGuiLifetime() { ImGui::CreateContext(); }
            ~ImGuiLifetime() { ImGui::DestroyContext(); }
        } lifetime;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 700};
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        CometEditor::RenderStatsPanel panel(*engine);
        const auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({10, 10});
            ImGui::SetNextWindowSize({700, 650});
            panel.render();
            ImGui::Render();
        };
        EXPECT_FALSE(panel.take_capture_request());
        EXPECT_FALSE(panel.take_allocation_report_request());
        frame();
        EXPECT_EQ(ImGui::GetDrawData()->TotalVtxCount, 0);
        panel.set_visible(true);
        frame();
        frame();
        const auto* window = ImGui::FindWindowByName("Render Stats");
        ASSERT_NE(window, nullptr);
        const auto capture = ImVec2(window->Pos.x + window->WindowPadding.x + 7,
            window->Pos.y + window->TitleBarHeight + window->WindowPadding.y + 7);
        const auto click = [&](ImVec2 point) {
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        click(capture);
        EXPECT_EQ(panel.take_capture_request(), true);
        EXPECT_FALSE(panel.take_capture_request());
        EXPECT_FALSE(panel.take_allocation_report_request());
        const auto report =
            ImVec2(capture.x + 20, capture.y + ImGui::GetFrameHeightWithSpacing());
        click(report);
        EXPECT_TRUE(panel.take_allocation_report_request());
        EXPECT_FALSE(panel.take_allocation_report_request());
        frame();
        EXPECT_FALSE(panel.take_capture_request());
        EXPECT_FALSE(panel.take_allocation_report_request());
    }
#endif
}
