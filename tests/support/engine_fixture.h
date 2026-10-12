#pragma once

#include "config/config.h"
#include "core/engine.h"
#include "diagnostics/logger.h"
#include "render/render_context.h"
#include "render/renderer.h"
#include "render/render_diagnostics.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <algorithm>
#include <memory>
#include <sstream>

namespace Comet::Tests {
    // 直接录制 GPU 测试仍在 Main 解析资产，与 Engine 的帧循环保持相同边界。
    class RenderTestFrontend {
    public:
        explicit RenderTestFrontend(Engine& engine)
            : m_engine(engine), m_resolver(engine.get_asset_registry()) {}
        Result<Renderer::FramePreparation, GraphicsError> prepare_frame() {
            m_resolver.refresh_assets();
            return m_engine.get_renderer().prepare_frame(
                m_engine.get_asset_registry().take_removals());
        }
        std::shared_ptr<const RenderWorld> resolve(const RenderScene& scene) {
            return RenderDiagnostics::measure_preparation(
                &m_engine.get_renderer().get_diagnostics(),
                RenderDiagnostics::PreparationPhase::Assets,
                [&] { return m_resolver.update(scene); });
        }
        Result<void, GraphicsError> render(const RenderScene& scene) {
            return m_engine.get_renderer().render_frame(resolve(scene));
        }

    private:
        Engine& m_engine;
        SceneResolver m_resolver;
    };

    inline Result<void, Error> recreate_test_engine(std::unique_ptr<Engine>& engine,
        std::unique_ptr<RenderTestFrontend>& frontend, const Config& config) {
        frontend.reset();
        engine.reset();
        auto created = Engine::create(config);
        if(!created)
            return Result<void, Error>::failure(created.error());
        engine = std::move(created).value();
        frontend = std::make_unique<RenderTestFrontend>(*engine);
        return Result<void, Error>::success();
    }

    // 故障注入与原生后端测试的录制动作；生产宿主交付具体的 UI 帧对象。
    class CallbackOverlayDraw final: public OverlayDraw {
    public:
        using Record = std::function<Result<void, GraphicsError>(OverlayRecordContext&)>;
        explicit CallbackOverlayDraw(Record record) : m_record(std::move(record)) {}
        Result<void, GraphicsError> record(OverlayRecordContext& frame) override {
            return m_record(frame);
        }

    private:
        Record m_record;
    };
    struct TestOverlay {
        CallbackOverlayDraw::Record render;
        std::function<void()> release;
        std::function<Result<void, GraphicsError>(const SwapchainCompatibility&)> rebuild;
        std::function<void()> shutdown;
    };
    inline void set_test_overlay(Renderer& renderer, TestOverlay overlay) {
        renderer.set_overlay({.capture =
                                  [record = std::move(overlay.render)] {
                                      OverlayDrawList draws;
                                      draws.push_back(
                                          std::make_unique<CallbackOverlayDraw>(record));
                                      return draws;
                                  },
            .release = std::move(overlay.release),
            .rebuild = std::move(overlay.rebuild),
            .shutdown = std::move(overlay.shutdown)});
    }
    inline Result<void, GraphicsError> record_draw_frame(
        OverlayRecordContext& frame, std::unique_ptr<OverlayDraw> draw) {
        if(draw)
            return draw->record(frame);
        return Result<void, GraphicsError>::success();
    }

    class VulkanValidationTest: public testing::Test {
    protected:
        void SetUp() override {
            m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            Logger::add_custom_sink(m_sink);
        }

        void TearDown() override {
            if(auto logger = Logger::get_console_logger())
                std::erase(logger->sinks(), m_sink);
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("hazard detected"), std::string::npos) << messages.str();
        }

        std::ostringstream messages;

    private:
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
    };

    class EngineTest: public VulkanValidationTest {
    protected:
        void SetUp() override {
            VulkanValidationTest::SetUp();
            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            const auto created = recreate_engine(config);
            ASSERT_TRUE(created) << created.error().message;
        }

        Result<void, Error> recreate_engine(const Config& config) {
            return recreate_test_engine(engine, render_frontend, config);
        }

        void TearDown() override {
            if(engine)
                engine->get_renderer().get_render_context().wait_idle();
            render_frontend.reset();
            engine.reset();
            VulkanValidationTest::TearDown();
        }

        std::unique_ptr<Engine> engine;
        std::unique_ptr<RenderTestFrontend> render_frontend;
    };
}
