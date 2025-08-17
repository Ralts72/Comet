#include <gtest/gtest.h>
#include "../../engine/src/core/math_utils.h"
#include <vector>
#include <random>

namespace Comet::Tests {

// 性能测试类
class MathPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 生成测试数据
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(-100.0f, 100.0f);

        for (int i = 0; i < 10000; ++i) {
            test_vectors.emplace_back(dis(gen), dis(gen), dis(gen));
        }
    }

    std::vector<Vec3> test_vectors;
};

TEST_F(MathPerformanceTest, VectorOperationsPerformance) {
    // 测试大量向量运算的性能
    auto start = std::chrono::high_resolution_clock::now();

    float total_length = 0.0f;
    for (const auto& vec : test_vectors) {
        total_length += length(vec);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // 性能测试：10000个向量运算应该在合理时间内完成（比如<10ms）
    EXPECT_LT(duration.count(), 10000); // 小于10毫秒
    EXPECT_GT(total_length, 0.0f); // 确保计算有结果
}

TEST_F(MathPerformanceTest, MatrixMultiplicationPerformance) {
    Mat4 transform = translate(Vec3(1.0f, 2.0f, 3.0f)) *
                    rotate(PI/4.0f, Vec3(0.0f, 1.0f, 0.0f)) *
                    scale(Vec3(2.0f, 2.0f, 2.0f));

    auto start = std::chrono::high_resolution_clock::now();

    // 大量矩阵乘法运算
    Mat4 result = Mat4(1.0f);
    for (int i = 0; i < 1000; ++i) {
        result = result * transform;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    EXPECT_LT(duration.count(), 5000); // 小于5毫秒
    EXPECT_FALSE(result == Mat4(0.0f)); // 确保结果不为零矩阵
}

} // namespace Comet::Tests
