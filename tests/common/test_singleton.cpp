#include <gtest/gtest.h>
#include "common/singleton.h"
#include <string>

namespace Comet::Tests {

// 测试用的简单类
class TestClass {
public:
    TestClass() : value_(0) {}
    explicit TestClass(int value) : value_(value) {}
    TestClass(int value, const std::string& name) : value_(value), name_(name) {}

    int getValue() const { return value_; }
    const std::string& getName() const { return name_; }
    void setValue(int value) { value_ = value; }

private:
    int value_;
    std::string name_;
};

// 自动初始化的单例
using AutoSingleton = Singleton<TestClass, false>;

// 显式初始化的单例
using ExplicitSingleton = Singleton<TestClass, true>;

class SingletonTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 确保显式单例在测试前被清理
        ExplicitSingleton::shutdown();
    }

    void TearDown() override {
        // 清理显式单例
        ExplicitSingleton::shutdown();
    }
};

TEST_F(SingletonTest, AutoSingletonBasicUsage) {
    // 测试自动单例的基本使用
    TestClass& instance1 = AutoSingleton::instance();
    TestClass& instance2 = AutoSingleton::instance();

    // 应该是同一个实例
    EXPECT_EQ(&instance1, &instance2);

    // 修改一个实例应该影响另一个
    instance1.setValue(42);
    EXPECT_EQ(instance2.getValue(), 42);
}

TEST_F(SingletonTest, AutoSingletonPersistence) {
    // 测试自动单例在多次调用中的持久性
    {
        TestClass& instance = AutoSingleton::instance();
        instance.setValue(100);
    }

    {
        TestClass& instance = AutoSingleton::instance();
        EXPECT_EQ(instance.getValue(), 100);
    }
}

TEST_F(SingletonTest, ExplicitSingletonInitialization) {
    // 测试显式单例的初始化
    ExplicitSingleton::init(42);

    TestClass& instance = ExplicitSingleton::instance();
    EXPECT_EQ(instance.getValue(), 42);
}

TEST_F(SingletonTest, ExplicitSingletonInitializationWithMultipleArgs) {
    // 测试显式单例使用多个参数初始化
    ExplicitSingleton::init(123, "TestName");

    TestClass& instance = ExplicitSingleton::instance();
    EXPECT_EQ(instance.getValue(), 123);
    EXPECT_EQ(instance.getName(), "TestName");
}

TEST_F(SingletonTest, ExplicitSingletonShutdown) {
    // 测试显式单例的关闭
    ExplicitSingleton::init(50);

    // 验证实例存在
    TestClass& instance = ExplicitSingleton::instance();
    EXPECT_EQ(instance.getValue(), 50);

    // 关闭单例
    ExplicitSingleton::shutdown();

    // 注意：在实际应用中，调用已关闭单例的instance()会导致断言失败
    // 这里我们不测试这种情况，因为它会终止程序
}

TEST_F(SingletonTest, ExplicitSingletonReinitialize) {
    // 测试显式单例的重新初始化
    ExplicitSingleton::init(10);
    TestClass& instance1 = ExplicitSingleton::instance();
    EXPECT_EQ(instance1.getValue(), 10);

    ExplicitSingleton::shutdown();
    ExplicitSingleton::init(20);

    TestClass& instance2 = ExplicitSingleton::instance();
    EXPECT_EQ(instance2.getValue(), 20);
}

TEST_F(SingletonTest, ExplicitSingletonConsistency) {
    // 测试显式单例的一致性
    ExplicitSingleton::init(75);

    TestClass& instance1 = ExplicitSingleton::instance();
    TestClass& instance2 = ExplicitSingleton::instance();

    // 应该是同一个实例
    EXPECT_EQ(&instance1, &instance2);

    // 修改应该在所有引用中可见
    instance1.setValue(99);
    EXPECT_EQ(instance2.getValue(), 99);
}

// 测试不同类型的单例不会互相干扰
class AnotherTestClass {
public:
    AnotherTestClass() : data_("default") {}
    explicit AnotherTestClass(const std::string& data) : data_(data) {}

    const std::string& getData() const { return data_; }
    void setData(const std::string& data) { data_ = data; }

private:
    std::string data_;
};

using AnotherAutoSingleton = Singleton<AnotherTestClass, false>;
using AnotherExplicitSingleton = Singleton<AnotherTestClass, true>;

TEST_F(SingletonTest, MultipleSingletonTypes) {
    // 测试不同类型的单例不会互相干扰

    // 初始化不同类型��单例
    ExplicitSingleton::init(42);
    AnotherExplicitSingleton::init("test_data");

    // 获取实��
    TestClass& testInstance = ExplicitSingleton::instance();
    AnotherTestClass& anotherInstance = AnotherExplicitSingleton::instance();

    // 验证它们是独立的
    EXPECT_EQ(testInstance.getValue(), 42);
    EXPECT_EQ(anotherInstance.getData(), "test_data");

    // 修改一个不应该影响另一个
    testInstance.setValue(100);
    anotherInstance.setData("modified");

    EXPECT_EQ(testInstance.getValue(), 100);
    EXPECT_EQ(anotherInstance.getData(), "modified");

    // 清理
    AnotherExplicitSingleton::shutdown();
}

TEST_F(SingletonTest, AutoAndExplicitSingletonCoexistence) {
    // 测试自动和显式单例可以共存

    // 使用自动单例
    TestClass& autoInstance = AutoSingleton::instance();
    autoInstance.setValue(200);

    // 初始化显式单例
    ExplicitSingleton::init(300);
    TestClass& explicitInstance = ExplicitSingleton::instance();

    // 它们应该是不同的实例
    EXPECT_NE(&autoInstance, &explicitInstance);
    EXPECT_EQ(autoInstance.getValue(), 200);
    EXPECT_EQ(explicitInstance.getValue(), 300);
}

} // namespace Comet::Tests
