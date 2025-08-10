#include <gtest/gtest.h>
#include "core/engine.h"
#include "common/log_system/log_system.h"

namespace Comet::Tests {

class EngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 确保在测试前清理引擎单例
        Engine::shutdown();
    }

    void TearDown() override {
        // 测试后清理引擎单例
        Engine::shutdown();
    }
};

TEST_F(EngineTest, SingletonBehavior) {
    // 测试引擎是否正确实现单例模式
    Engine::init();

    Engine& instance1 = Engine::instance();
    Engine& instance2 = Engine::instance();

    // 应该是同一个实例
    EXPECT_EQ(&instance1, &instance2);
}

TEST_F(EngineTest, InitializationAndShutdown) {
    // 测试引擎的初始化和关闭
    EXPECT_NO_THROW(Engine::init());

    Engine& engine = Engine::instance();

    // 引擎应该正确初始化
    EXPECT_NE(engine.get_window(), nullptr);

    // 测试关闭
    EXPECT_NO_THROW(Engine::shutdown());
}

TEST_F(EngineTest, WindowAccess) {
    Engine::init();
    Engine& engine = Engine::instance();

    // 应该有有效的窗口指针
    Window* window = engine.get_window();
    EXPECT_NE(window, nullptr);
}

TEST_F(EngineTest, InitialCloseStatus) {
    Engine::init();
    Engine& engine = Engine::instance();

    // 初始状态下引擎不应该标记为关闭
    EXPECT_FALSE(engine.get_close_status());
}

TEST_F(EngineTest, UpdateLoop) {
    Engine::init();
    Engine& engine = Engine::instance();

    // 测试更新循环不会抛出异常
    EXPECT_NO_THROW(engine.on_update());

    // 多次调用更新
    for (int i = 0; i < 5; ++i) {
        EXPECT_NO_THROW(engine.on_update());
    }
}

TEST_F(EngineTest, MultipleInitialization) {
    // 测试多次初始化的行为
    Engine::init();
    Engine& engine1 = Engine::instance();

    Engine::shutdown();
    Engine::init();
    Engine& engine2 = Engine::instance();

    // 重新初始化后应该得到新的实例
    EXPECT_NE(&engine1, &engine2);
}

// 注意：这些测试可能需要根据实际的Engine实现进行调整
// 如果Engine的构造函数需要特定的参数或环境设置，需要相应修改

} // namespace Comet::Tests
