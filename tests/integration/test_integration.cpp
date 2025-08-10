#include <gtest/gtest.h>
#include "core/engine.h"
#include "common/log_system/log_system.h"

namespace Comet::Tests {

// 集成测试：测试多个模块之间的协作
class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 确保测试前清理状态
        Engine::shutdown();
    }

    void TearDown() override {
        // 测试后清理
        Engine::shutdown();
    }
};

TEST_F(IntegrationTest, EngineInitializationSequence) {
    // 测试完整的引擎初始化序列

    // 1. 日志系统应该可用
    LogSystem& logSystem = LogSystem::instance();
    EXPECT_NE(logSystem.m_console_logger, nullptr);

    // 2. 初始化引擎
    EXPECT_NO_THROW(Engine::init());

    // 3. 引擎应该正确初始化
    Engine& engine = Engine::instance();
    EXPECT_NE(engine.get_window(), nullptr);
    EXPECT_FALSE(engine.get_close_status());

    // 4. 窗口应该有正确的初始状态
    Window* window = engine.get_window();
    EXPECT_FALSE(window->should_close());

    Vec2i size = window->get_size();
    EXPECT_GT(size.x, 0);
    EXPECT_GT(size.y, 0);
}

TEST_F(IntegrationTest, EngineUpdateCycle) {
    // 测试引擎的更新循环
    Engine::init();
    Engine& engine = Engine::instance();

    // 模拟多帧更新
    for (int frame = 0; frame < 10; ++frame) {
        EXPECT_NO_THROW(engine.on_update());

        // 每帧后窗口应该仍然有效
        EXPECT_NE(engine.get_window(), nullptr);

        // 如果窗口被标记为关闭，循环应该能够检测到
        if (engine.get_close_status()) {
            break;
        }
    }
}

TEST_F(IntegrationTest, MathAndEngineIntegration) {
    // 测试数学库与引擎的集成
    Engine::init();
    Engine& engine = Engine::instance();

    Window* window = engine.get_window();
    Vec2i windowSize = window->get_size();

    // 使用窗口尺寸进行数学计算
    float aspect = static_cast<float>(windowSize.x) / static_cast<float>(windowSize.y);
    Mat4 projMatrix = perspective(45.0f, aspect, 0.1f, 100.0f);

    // 验证投影矩阵不为零
    EXPECT_FALSE(projMatrix == Mat4(0.0f));

    // 测试变换矩阵链
    Mat4 modelMatrix = translate(Vec3(0.0f, 0.0f, -5.0f)) *
                      rotate(PI/4.0f, Vec3(0.0f, 1.0f, 0.0f)) *
                      scale(Vec3(1.0f, 1.0f, 1.0f));

    Mat4 mvpMatrix = projMatrix * modelMatrix;
    EXPECT_FALSE(mvpMatrix == Mat4(0.0f));
}

TEST_F(IntegrationTest, LoggingThroughoutSystem) {
    // 测试整个系统中的日志记录

    // 记录引擎初始化
    LOG_INFO("Starting engine initialization test");

    Engine::init();
    LOG_INFO("Engine initialized successfully");

    Engine& engine = Engine::instance();
    Window* window = engine.get_window();

    Vec2i size = window->get_size();
    LOG_INFO("Window created with size: {}x{}", size.x, size.y);

    // 模拟一些更新
    for (int i = 0; i < 3; ++i) {
        LOG_DEBUG("Update frame {}", i);
        engine.on_update();
    }

    LOG_INFO("Integration test completed successfully");

    // 如果到这里没有崩溃，说明日志系统与其他模块集成良好
    SUCCEED();
}

TEST_F(IntegrationTest, ResourceManagement) {
    // 测试资源管理和生命周期

    {
        // 在嵌套作用域中创建引擎
        Engine::init();
        Engine& engine = Engine::instance();

        // 验证资源创建
        EXPECT_NE(engine.get_window(), nullptr);

        // 模拟使用
        engine.on_update();
    }

    // 手动清理
    Engine::shutdown();

    // 重新初始化应该工作正常
    EXPECT_NO_THROW(Engine::init());

    Engine& newEngine = Engine::instance();
    EXPECT_NE(newEngine.get_window(), nullptr);
}

TEST_F(IntegrationTest, ErrorHandlingAcrossModules) {
    // 测试跨模块的错误处理

    // 测试在引擎未初始化时访问（如果适用）
    // 这取决于具体的实现策略

    Engine::init();
    Engine& engine = Engine::instance();

    // 测试引擎在异常情况下的行为
    // 例如：多次更新、快速初始化/关闭等

    for (int i = 0; i < 5; ++i) {
        EXPECT_NO_THROW(engine.on_update());
    }

    // 测试清理后的状态
    Engine::shutdown();

    // 重新初始化应该恢复正常状态
    EXPECT_NO_THROW(Engine::init());
}

} // namespace Comet::Tests
