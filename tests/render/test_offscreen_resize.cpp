#include "support/engine_fixture.h"
#include "render/scene/scene_renderer.h"
#include "render/render_target.h"
#include "graphics/device.h"

#include <chrono>

namespace Comet::Tests {
    class OffscreenResizeTest: public EngineTest {
    protected:
        void SetUp() override {
            owns_logger = !Logger::get_console_logger();
            if(owns_logger)
                Logger::init();
            EngineTest::SetUp();
            auto logger = Logger::get_console_logger();
            previous_level = logger->level();
            logger->set_level(spdlog::level::warn);
        }
        void TearDown() override {
            EngineTest::TearDown();
            if(owns_logger)
                Logger::shutdown();
            else
                Logger::get_console_logger()->set_level(previous_level);
        }

    private:
        spdlog::level::level_enum previous_level;
        bool owns_logger = false;
    };

    TEST_F(OffscreenResizeTest, RejectsRepeatedInvalidSizeAndPreservesActualTarget) {
        auto& renderer = engine->get_renderer();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({160, 120}));
        auto& scene = renderer.get_scene_renderer();
        auto* previous = &scene.get_render_target();
        const auto previous_view = scene.get_offscreen_color_view(0);
        const auto limit =
            renderer.get_render_context().get_device().get_capability().max_image_dimension_2d;
        const Math::Vec2u invalid_size{limit + 1, 120};
        auto now = std::chrono::steady_clock::time_point{};
        scene.resize_offscreen_target(invalid_size, now);
        const auto first_log = messages.str();
        EXPECT_NE(first_log.find("waiting for a new size"), std::string::npos);
        for(int frame = 0; frame < 100; ++frame) {
            now += std::chrono::seconds(1);
            scene.resize_offscreen_target(invalid_size, now);
        }
        EXPECT_EQ(messages.str(), first_log);
        EXPECT_EQ(&scene.get_render_target(), previous);
        EXPECT_EQ(scene.get_offscreen_color_view(0), previous_view);
        EXPECT_EQ(scene.get_render_target().get_size(), Math::Vec2u(160, 120));

        scene.resize_offscreen_target({200, 140}, now);
        EXPECT_EQ(scene.get_render_target().get_size(), Math::Vec2u(200, 140));
        EXPECT_NE(scene.get_offscreen_color_view(0), previous_view);
        const auto installed_view = scene.get_offscreen_color_view(0);
        scene.resize_offscreen_target({200, 140}, now);
        EXPECT_EQ(scene.get_offscreen_color_view(0), installed_view);

        // 成功安装清除旧失败状态，同一非法尺寸作为新请求会重新诊断。
        const auto before = messages.str().size();
        scene.resize_offscreen_target(invalid_size, now);
        EXPECT_GT(messages.str().size(), before);
        EXPECT_EQ(scene.get_render_target().get_size(), Math::Vec2u(200, 140));

        scene.resize_offscreen_target({200, 140}, now);
        const auto canceled = messages.str().size();
        scene.resize_offscreen_target(invalid_size, now);
        EXPECT_GT(messages.str().size(), canceled);
    }
}
