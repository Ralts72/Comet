#include <gtest/gtest.h>
#include "test_utils.h"
#include "common/math_utils.h"

namespace Comet::Tests {

// 使用测试工具类的数学测试
class AdvancedMathTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(AdvancedMathTest, TestUtilsFloatComparison) {
    // 测试浮点数比较工具
    EXPECT_TRUE(TestUtils::FloatEqual(1.0f, 1.0f));
    EXPECT_TRUE(TestUtils::FloatEqual(1.0f, 1.000001f, 0.001f));
    EXPECT_FALSE(TestUtils::FloatEqual(1.0f, 1.1f));
}

TEST_F(AdvancedMathTest, TestUtilsVectorComparison) {
    // 测试向量比较工具
    Vec3 v1(1.0f, 2.0f, 3.0f);
    Vec3 v2(1.0f, 2.0f, 3.0f);
    Vec3 v3(1.1f, 2.0f, 3.0f);

    EXPECT_VEC3_EQ(v1, v2);
    EXPECT_FALSE(TestUtils::Vec3Equal(v1, v3));
}

TEST_F(AdvancedMathTest, TestUtilsMatrixComparison) {
    // 测试矩阵比较工具
    Mat4 identity(1.0f);
    Mat4 another_identity(1.0f);

    EXPECT_MAT4_EQ(identity, another_identity);
    EXPECT_TRUE(TestUtils::IsIdentityMatrix(identity));
}

TEST_F(AdvancedMathTest, NormalizationValidation) {
    // 测试归一化验证工具
    Vec3 unit_x(1.0f, 0.0f, 0.0f);
    Vec3 random_vec(3.0f, 4.0f, 0.0f);
    Vec3 normalized_random = normalize(random_vec);

    EXPECT_NORMALIZED(unit_x);
    EXPECT_NORMALIZED(normalized_random);
}

TEST_F(AdvancedMathTest, TransformationBuilder) {
    // 测试变换矩阵构建工具
    Vec3 translation(1.0f, 2.0f, 3.0f);
    Vec3 rotation(0.0f, PI/2.0f, 0.0f);
    Vec3 scale_vec(2.0f, 2.0f, 2.0f);

    Mat4 transform = TestUtils::CreateTestTransform(translation, rotation, scale_vec);

    // 验证变换矩阵不是零矩阵或单位矩阵
    EXPECT_FALSE(TestUtils::Mat4Equal(transform, Mat4(0.0f)));
    EXPECT_FALSE(TestUtils::IsIdentityMatrix(transform));
}

TEST_F(AdvancedMathTest, RandomVectorGeneration) {
    // 测试随机向量生成
    auto vectors = TestUtils::GenerateRandomVectors(100, -10.0f, 10.0f);

    EXPECT_EQ(vectors.size(), 100);

    // 验证所有向量都在指定范围内
    for (const auto& vec : vectors) {
        EXPECT_TRUE(TestUtils::IsInRange(vec.x, -10.0f, 10.0f));
        EXPECT_TRUE(TestUtils::IsInRange(vec.y, -10.0f, 10.0f));
        EXPECT_TRUE(TestUtils::IsInRange(vec.z, -10.0f, 10.0f));
    }
}

TEST_F(AdvancedMathTest, PerformanceMeasurement) {
    // 测试性能测量工具
    std::vector<Vec3> vectors = TestUtils::GenerateRandomVectors(1000);

    EXPECT_PERFORMANCE({
        for (const auto& vec : vectors) {
            Vec3 normalized = normalize(vec);
            float len = length(normalized);
            (void)len; // 避免未使用变量警告
        }
    }, 5.0); // 期望在5毫秒内完成
}

TEST_F(AdvancedMathTest, ComplexTransformChain) {
    // 测试复杂的变换链
    Mat4 final_transform = Mat4(1.0f);

    // 应用多个变换
    final_transform = translate(Vec3(1.0f, 0.0f, 0.0f)) * final_transform;
    final_transform = rotate(PI/4.0f, Vec3(0.0f, 1.0f, 0.0f)) * final_transform;
    final_transform = scale(Vec3(2.0f, 2.0f, 2.0f)) * final_transform;

    // 验证结果
    Vec4 point(0.0f, 0.0f, 0.0f, 1.0f);
    Vec4 transformed = final_transform * point;

    // 变换后的点应该不等于原点
    EXPECT_FALSE(TestUtils::Vec3Equal(Vec3(transformed), Vec3(0.0f)));
}

} // namespace Comet::Tests
