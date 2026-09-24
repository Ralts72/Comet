#include "support/render_gpu_test.h"
#include "render/render_diagnostics.h"
#include "graphics/gpu_timer.h"
#include "core/window.h"

#include <limits>

#ifdef COMET_TEST_EDITOR_UI
#include "common/scope_exit.h"
#include "render/render_stats.h"
#include "ui/imgui_context.h"
#include "support/imgui_context.h"
#include "support/temporary_directory.h"
#include <imgui_internal.h>
#endif

namespace Comet::Tests {
    using RenderDiagnosticsGpuTest = RenderGpuTest;

    TEST(GpuTimerTest, ConvertsValidBitsPeriodAndWraparound) {
        EXPECT_DOUBLE_EQ(*GpuTimer::elapsed_ms(100, 1100, {64, 2}), 0.002);
        EXPECT_DOUBLE_EQ(*GpuTimer::elapsed_ms(250, 5, {8, 1000}), 0.011);
        EXPECT_DOUBLE_EQ(
            *GpuTimer::elapsed_ms(std::numeric_limits<uint64_t>::max() - 4, 3, {64, 1000}), 0.008);
        for(auto bits : {0u, 65u})
            EXPECT_FALSE(GpuTimer::elapsed_ms(0, 1, {bits, 1}));
        for(auto period : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()})
            EXPECT_FALSE(GpuTimer::elapsed_ms(0, 1, {64, period}));
    }

    TEST_F(RenderDiagnosticsGpuTest, ReadsOnlyConfirmedSubmissionsAcrossSlotReuse) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        RenderDiagnostics diagnostics(frames);
        ASSERT_TRUE(diagnostics.set_enabled(true));
        if(!diagnostics.get_snapshot().gpu_supported)
            GTEST_SKIP() << "Graphics timestamps unavailable; CPU fallback is tested separately";
        auto buffer =
            Buffer::create_gpu_buffer(device, Flags<BufferUsage>(BufferUsage::CopyDst), 4096);
        for(uint64_t index = 0; index < 5; ++index) {
            RenderGraph graph;
            const auto data = graph.import_buffer("data", {{}, 0, buffer->get_size()});
            const auto count = index % 2 + 2;
            for(size_t pass = 0; pass < count; ++pass)
                graph.add_pass({"frame " + std::to_string(index) + " pass " + std::to_string(pass),
                    {{data, ResourceUsage::TransferDestination, {}}}});
            auto plan = graph.compile();
            ASSERT_TRUE(plan) << plan.error();
            frames.wait_for_current_slot();
            frames.begin_frame(static_cast<uint32_t>(index % 2));
            frames.get_current_command_buffer().begin();
            const std::vector<RenderGraph::Binding> bindings{buffer};
            ASSERT_TRUE(diagnostics.record(
                plan.value(), bindings, [&](size_t pass, CommandBuffer& command) {
                    command.get().fillBuffer(
                        buffer->get(), 0, buffer->get_size(), static_cast<uint32_t>(pass));
                    return Result<void, GraphicsError>::success();
                }));
            const auto& snapshot = diagnostics.get_snapshot();
            ASSERT_TRUE(snapshot.cpu);
            EXPECT_EQ(snapshot.cpu->serial, index + 1);
            EXPECT_EQ(snapshot.cpu->passes.size(), count);
            ASSERT_TRUE(diagnostics.collect_completed());
            EXPECT_EQ(snapshot.gpu ? snapshot.gpu->serial : 0, index);
            submit(device, frames);
            ASSERT_TRUE(diagnostics.collect_completed());
            EXPECT_EQ(snapshot.gpu ? snapshot.gpu->serial : 0, index);
            frames.wait_for_all_slots();
            ASSERT_TRUE(diagnostics.collect_completed());
            ASSERT_TRUE(snapshot.gpu) << snapshot.gpu_error;
            EXPECT_TRUE(snapshot.gpu_error.empty());
            EXPECT_EQ(snapshot.gpu->serial, index + 1);
            EXPECT_EQ(
                snapshot.gpu->passes.front().name, "frame " + std::to_string(index) + " pass 0");
            double sum = 0;
            for(const auto& pass : snapshot.gpu->passes) {
                EXPECT_GE(pass.milliseconds, 0);
                sum += pass.milliseconds;
            }
            EXPECT_GE(snapshot.gpu->milliseconds + 1e-6, sum);
        }
    }

    TEST_F(RenderDiagnosticsGpuTest, CpuFallbackBoundsDetailsAndDisabledCaptureDoesNoSampling) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RenderDiagnostics diagnostics(frames, false);
        EXPECT_FALSE(diagnostics.get_snapshot().gpu_supported);
        RenderGraph graph;
        for(unsigned index = 0; index < RenderDiagnostics::MAX_PASSES + 3; ++index)
            graph.add_pass({std::to_string(index) + std::string(200, 'x'), {}});
        auto plan = graph.compile();
        ASSERT_TRUE(plan) << plan.error();
        unsigned calls = 0;
        const auto record = [&](size_t, CommandBuffer&) {
            ++calls;
            EXPECT_FALSE(diagnostics.set_enabled(false));
            EXPECT_FALSE(diagnostics.collect_completed());
            EXPECT_FALSE(diagnostics.record(plan.value(), {},
                [](size_t, CommandBuffer&) { return Result<void, GraphicsError>::success(); }));
            return Result<void, GraphicsError>::success();
        };
        EXPECT_FALSE(diagnostics.record(plan.value(), {}, record));
        for(const bool enabled : {true, false}) {
            ASSERT_TRUE(diagnostics.set_enabled(enabled));
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            ASSERT_TRUE(diagnostics.record(plan.value(), {}, record));
            if(enabled) {
                EXPECT_FALSE(diagnostics.record(plan.value(), {}, record));
                ASSERT_TRUE(diagnostics.get_snapshot().cpu);
                EXPECT_TRUE(diagnostics.get_snapshot().cpu->truncated);
                EXPECT_EQ(
                    diagnostics.get_snapshot().cpu->passes.size(), RenderDiagnostics::MAX_PASSES);
                EXPECT_EQ(diagnostics.get_snapshot().cpu->passes.front().name.size(),
                    RenderDiagnostics::MAX_LABEL_LENGTH);
            }
            submit(device, frames);
        }
        EXPECT_EQ(calls, 2 * (RenderDiagnostics::MAX_PASSES + 3));
        EXPECT_EQ(diagnostics.get_snapshot().cpu->serial, 1);
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 1);
        EXPECT_FALSE(diagnostics.get_snapshot().gpu);
    }

    TEST_F(RenderDiagnosticsGpuTest, FailedRecordingDoesNotPublishSampleOrSubmitPartialFrame) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        RenderDiagnostics diagnostics(frames);
        ASSERT_TRUE(diagnostics.set_enabled(true));
        RenderGraph graph;
        graph.add_pass({"failed", {}});
        auto plan = graph.compile();
        ASSERT_TRUE(plan);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        auto recorded = diagnostics.record(plan.value(), {}, [](size_t, CommandBuffer&) {
            return Result<void, GraphicsError>::failure({"injected recorder failure"});
        });
        ASSERT_FALSE(recorded);
        EXPECT_EQ(recorded.error().message, "injected recorder failure");
        ASSERT_TRUE(diagnostics.collect_completed());
        EXPECT_FALSE(diagnostics.get_snapshot().cpu);
        EXPECT_FALSE(diagnostics.get_snapshot().gpu);
        EXPECT_EQ(frames.get_completed_frame_serial(), 0);
        // 失败帧只结束录制并丢弃，不交给会自动提交的 FrameWait。
        frames.get_current_command_buffer().end();
    }

    TEST_F(RenderDiagnosticsGpuTest, QueryOwnerOutlivesDiagnosticsUntilSubmissionCompletes) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RenderGraph graph;
        graph.add_pass({"measured", {}});
        auto plan = graph.compile();
        ASSERT_TRUE(plan);
        auto diagnostics = std::make_unique<RenderDiagnostics>(frames);
        ASSERT_TRUE(diagnostics->set_enabled(true));
        if(!diagnostics->get_snapshot().gpu_supported)
            GTEST_SKIP() << "Graphics timestamps unavailable";
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        ASSERT_TRUE(diagnostics->record(plan.value(), {},
            [](size_t, CommandBuffer&) { return Result<void, GraphicsError>::success(); }));
        diagnostics.reset();
        submit(device, frames);
        frames.wait_for_all_slots();
    }

    TEST_F(RenderDiagnosticsGpuTest, MemoryPollingIsBoundedAndReportReflectsLiveAllocations) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 1);
        RenderDiagnostics diagnostics(frames, false);
        const auto start = RenderDiagnostics::Clock::time_point{} + std::chrono::seconds(100);
        diagnostics.poll_memory(start);
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 0);
        ASSERT_TRUE(diagnostics.set_enabled(true));
        diagnostics.poll_memory(start);
        for(int i = 1; i < 1000; ++i)
            diagnostics.poll_memory(start + std::chrono::milliseconds(i));
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 1);
        auto allocation = Buffer::create_gpu_buffer(
            device, Flags<BufferUsage>(BufferUsage::CopyDst), 4096, "diagnostics-report-test");
        diagnostics.poll_memory(start + std::chrono::seconds(1));
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 2);
        EXPECT_FALSE(diagnostics.get_snapshot().memory.heaps.empty());
        ASSERT_TRUE(diagnostics.set_enabled(false));
        diagnostics.poll_memory(start + std::chrono::seconds(10));
        auto report = diagnostics.build_allocation_report();
        ASSERT_TRUE(report) << report.error();
        EXPECT_NE(report.value().find("diagnostics-report-test"), std::string::npos);
        EXPECT_NE(report.value().find("\"Total\""), std::string::npos);
        allocation.reset();
        report = diagnostics.build_allocation_report();
        ASSERT_TRUE(report);
        EXPECT_EQ(report.value().find("diagnostics-report-test"), std::string::npos);
        EXPECT_EQ(diagnostics.get_snapshot().memory_samples, 2);
    }

    TEST_F(RenderDiagnosticsGpuTest, EngineMeasuresWallPhasesAndActualSceneGraph) {
        auto& renderer = engine->get_renderer();
        auto& diagnostics = renderer.get_diagnostics();
        ASSERT_TRUE(diagnostics.set_enabled(true));
        unsigned updates = 0;
        ASSERT_TRUE(engine->run([&](Engine::FrameContext&) {
            if(++updates == 4)
                engine->get_window().request_close();
            return Result<void, Error>::success();
        }));
        ASSERT_EQ(updates, 4);
        ASSERT_TRUE(engine->frame_diagnostics().current());
        const auto& timing = *engine->frame_diagnostics().current();
        EXPECT_TRUE(timing.rendered);
        EXPECT_GE(timing.events_ms, 0);
        EXPECT_GE(timing.update_ms, 0);
        EXPECT_GE(timing.prepare_ms, 0);
        EXPECT_GE(timing.render_submit_ms, 0);
        EXPECT_DOUBLE_EQ(timing.total_ms,
            timing.events_ms + timing.update_ms + timing.prepare_ms + timing.render_submit_ms);
        renderer.wait_idle();
        ASSERT_TRUE(diagnostics.collect_completed());
        ASSERT_TRUE(diagnostics.get_snapshot().cpu);
        EXPECT_EQ(diagnostics.get_snapshot().cpu->passes.size(), 3);
        if(diagnostics.get_snapshot().gpu_supported) {
            ASSERT_TRUE(diagnostics.get_snapshot().gpu) << diagnostics.get_snapshot().gpu_error;
            EXPECT_EQ(
                diagnostics.get_snapshot().gpu->serial, diagnostics.get_snapshot().cpu->serial);
        }
    }

#ifdef COMET_TEST_EDITOR_UI
    TEST_F(RenderDiagnosticsGpuTest, DisplayRefreshIsThrottledAndPauseDoesNotStopCollection) {
        ImGuiTestContext imgui({1000, 900});
        auto& renderer = engine->get_renderer();
        auto& diagnostics = renderer.get_diagnostics();
        ASSERT_TRUE(diagnostics.set_enabled(true));
        CometEditor::RenderStatsPanel panel(engine->frame_diagnostics(), diagnostics);
        const auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowSize({950, 850});
            ImGui::LogToBuffer();
            panel.render();
            const std::string text = GImGui->LogBuffer.c_str();
            ImGui::LogFinish();
            ImGui::Render();
            return text;
        };
        frame();
        auto* window = ImGui::FindWindowByName("Render Stats");
        ASSERT_NE(window, nullptr);
        window->StateStorage.SetInt(window->GetID("Render pass details"), 1);
        const auto before = frame();
        ASSERT_NE(before.find("Waiting for samples"), std::string::npos);
        const auto render_graph = [&] {
            auto prepared = renderer.prepare_frame();
            ASSERT_TRUE(prepared);
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
            ASSERT_TRUE(renderer.render_frame({}));
            renderer.wait_idle();
            ASSERT_TRUE(diagnostics.collect_completed());
        };
        render_graph();
        EXPECT_EQ(frame(), before);
        ImGui::GetIO().DeltaTime = 0.3f;
        const auto sampled = frame();
        EXPECT_NE(sampled, before);
        EXPECT_NE(sampled.find("CPU graph recording"), std::string::npos);

        ImGui::ActivateItemByID(window->GetID("Pause display"));
        const auto paused = frame();
        EXPECT_NE(paused.find("Display paused"), std::string::npos);
        ASSERT_TRUE(diagnostics.is_enabled());
        render_graph();
        const auto last_serial = diagnostics.get_snapshot().cpu->serial;
        EXPECT_EQ(frame(), paused);
        render_graph();
        EXPECT_GT(diagnostics.get_snapshot().cpu->serial, last_serial);
        EXPECT_EQ(frame(), paused);
        ASSERT_TRUE(diagnostics.set_enabled(false));
        frame();
        ImGui::ActivateItemByID(window->GetID("Pause display"));
        const auto stopped = frame();
        EXPECT_NE(stopped.find("Capture stopped"), std::string::npos);
        EXPECT_EQ(frame(), stopped);
        ASSERT_TRUE(diagnostics.set_enabled(true));
        EXPECT_EQ(diagnostics.cpu_history().summarize().total.count, 0);
        EXPECT_EQ(diagnostics.gpu_history().summarize().total.count, 0);
        EXPECT_NE(frame(), stopped);
    }

    TEST_F(RenderDiagnosticsGpuTest, StatsPanelRendersChineseWithEditorFont) {
        const auto translations = CometEditor::Ui::load_translations();
        ASSERT_TRUE(translations) << translations.error();
        const CometEditor::Ui::LanguageScope chinese(
            CometEditor::Ui::Language::Chinese, &translations.value());
        auto& renderer = engine->get_renderer();
        TemporaryDirectory directory;
        auto created = CometEditor::ImGuiContext::create(
            engine->get_window(), renderer.get_render_context(), directory.path() / "imgui.ini");
        ASSERT_TRUE(created) << created.error();
        auto& ui = *created.value();
        CometEditor::RenderStatsPanel panel(
            engine->frame_diagnostics(), renderer.get_diagnostics());
        panel.set_visible(true);
        renderer.set_overlay_renderer([&](CommandBuffer& command) { ui.render(command); });
        const ScopeExit finish([&] {
            renderer.set_overlay_renderer({});
            renderer.wait_idle();
        });
        for(unsigned frame = 0; frame < 2; ++frame) {
            auto prepared = renderer.prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error();
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
            ASSERT_TRUE(ui.begin_frame());
            EXPECT_STREQ(ImGui::GetFont()->GetDebugName(), "Roboto-Bold.ttf");
            for(const auto glyph : U"渲染统计采集显存堆预算阴影模糊耗时")
                if(glyph != 0)
                    EXPECT_TRUE(ImGui::GetFont()->IsGlyphInFont(static_cast<ImWchar>(glyph)));
            panel.render();
            ui.end_frame();
            auto rendered = renderer.render_frame({});
            ASSERT_TRUE(rendered) << rendered.error();
        }
    }

    TEST_F(RenderDiagnosticsGpuTest, StatsPanelStartsVisibleAndOnlyEmitsRequestsOnInteraction) {
        ImGuiTestContext imgui({800, 700});
        auto& io = ImGui::GetIO();
        CometEditor::RenderStatsPanel panel(
            engine->frame_diagnostics(), engine->get_renderer().get_diagnostics());
        const auto frame = [&] {
            ImGui::NewFrame();
            if(panel.is_open()) {
                ImGui::SetNextWindowPos({10, 10});
                ImGui::SetNextWindowSize({700, 650});
            }
            panel.render();
            ImGui::Render();
        };
        frame();
        EXPECT_TRUE(panel.is_open());
        EXPECT_FALSE(panel.take_capture_request());
        EXPECT_FALSE(panel.take_allocation_report_request());
        auto& diagnostics = engine->get_renderer().get_diagnostics();
        EXPECT_FALSE(diagnostics.is_enabled());
        ASSERT_TRUE(diagnostics.set_enabled(true));
        panel.set_visible(false);
        frame();
        EXPECT_FALSE(panel.is_open());
        EXPECT_TRUE(diagnostics.is_enabled());
        EXPECT_FALSE(panel.take_capture_request());
        EXPECT_FALSE(panel.take_allocation_report_request());
        panel.set_visible(true);
        frame();
        frame();
        auto* window = ImGui::FindWindowByName("Render Stats");
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
        EXPECT_EQ(panel.take_capture_request(), false);
        EXPECT_FALSE(panel.take_capture_request());
        ImGui::ActivateItemByID(window->GetID("Save allocation report"));
        frame();
        EXPECT_TRUE(panel.take_allocation_report_request());
        EXPECT_FALSE(panel.take_allocation_report_request());
        frame();
        EXPECT_FALSE(panel.take_capture_request());
        EXPECT_FALSE(panel.take_allocation_report_request());
        EXPECT_TRUE(diagnostics.is_enabled());
    }
#endif
}
