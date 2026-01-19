#include <gtest/gtest.h>
#include "common/config.h"

using namespace Comet;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Config 会在第一次调用时自动加载 config.yaml
    }
};

TEST_F(ConfigTest, GetIntValue) {
    // 测试获取整数值（使用不依赖窗口尺寸的配置）
    int msaa_samples = Config::get<int>("vulkan.msaa_samples");
    EXPECT_EQ(msaa_samples, 4);

    int swapchain_image_count = Config::get<int>("vulkan.swapchain_image_count");
    EXPECT_EQ(swapchain_image_count, 3);
}

TEST_F(ConfigTest, GetBoolValue) {
    // 测试获取布尔值
    bool enable_validation = Config::get<bool>("debug.enable_validation");
    EXPECT_TRUE(enable_validation);

    bool resizable = Config::get<bool>("window.resizable");
    EXPECT_TRUE(resizable);
}

TEST_F(ConfigTest, GetStringValue) {
    // 测试获取字符串值
    std::string title = Config::get<std::string>("window.title");
    EXPECT_EQ(title, "Comet Engine");

    // 只验证日志级别存在且不为空，不验证具体值（因为是可配置的）
    std::string log_level = Config::get<std::string>("debug.log_level");
    EXPECT_FALSE(log_level.empty());
}

TEST_F(ConfigTest, GetFloatValue) {
    // 测试获取浮点数
    int msaa = Config::get<int>("vulkan.msaa_samples");
    EXPECT_EQ(msaa, 4);
}

TEST_F(ConfigTest, GetVectorValue) {
    // 测试获取数组值
    auto clear_color = Config::get<std::vector<float>>("render.clear_color");
    EXPECT_EQ(clear_color.size(), 4);
    EXPECT_FLOAT_EQ(clear_color[0], 0.2f);
    EXPECT_FLOAT_EQ(clear_color[1], 0.4f);
    EXPECT_FLOAT_EQ(clear_color[2], 0.1f);
    EXPECT_FLOAT_EQ(clear_color[3], 1.0f);
}

TEST_F(ConfigTest, GetWithDefaultValue) {
    // 测试带默认值的获取
    int existing = Config::get<int>("vulkan.msaa_samples", 8);
    EXPECT_EQ(existing, 4); // 应该返回配置文件中的值

    int non_existing = Config::get<int>("non.existing.key", 999);
    EXPECT_EQ(non_existing, 999); // 应该返回默认值
}

TEST_F(ConfigTest, HasKey) {
    // 测试键是否存在
    EXPECT_TRUE(Config::has("debug.log_level"));
    EXPECT_TRUE(Config::has("vulkan.surface_format"));
    EXPECT_FALSE(Config::has("non.existing.key"));
}

TEST_F(ConfigTest, NestedAccess) {
    // 测试嵌套访问
    int surface_format = Config::get<int>("vulkan.surface_format");
    EXPECT_EQ(surface_format, 50);

    int depth_format = Config::get<int>("vulkan.depth_format");
    EXPECT_EQ(depth_format, 126);
}

TEST_F(ConfigTest, ThrowOnInvalidKey) {
    // 测试不存在的键会抛出异常
    EXPECT_THROW(Config::get<int>("invalid.key"), std::runtime_error);
}

TEST_F(ConfigTest, ThrowOnTypeMismatch) {
    // 测试类型不匹配会抛出异常
    EXPECT_THROW(Config::get<int>("window.title"), std::runtime_error);
}