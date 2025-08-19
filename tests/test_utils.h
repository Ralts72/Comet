#pragma once
#include <gtest/gtest.h>
#include "../engine/src/core/math_utils.h"
#include <chrono>
#include <random>
#include <vector>

namespace Comet::Tests {
    // 测试工具类，提供常用的测试辅助功��
    class TestUtils {
    public:
        // 浮点数比较工具
        static bool FloatEqual(float a, float b, float epsilon = 1e-6f) {
            return std::abs(a - b) < epsilon;
        }

        // 向量比较工具
        static bool Vec3Equal(const Math::Vec3& a, const Math::Vec3& b, float epsilon = 1e-6f) {
            return FloatEqual(a.x, b.x, epsilon) &&
                   FloatEqual(a.y, b.y, epsilon) &&
                   FloatEqual(a.z, b.z, epsilon);
        }

        static bool Vec2Equal(const Math::Vec2& a, const Math::Vec2& b, float epsilon = 1e-6f) {
            return FloatEqual(a.x, b.x, epsilon) &&
                   FloatEqual(a.y, b.y, epsilon);
        }

        // 矩阵比较工具
        static bool Mat4Equal(const Math::Mat4& a, const Math::Mat4& b, float epsilon = 1e-6f) {
            for(int i = 0; i < 4; ++i) {
                for(int j = 0; j < 4; ++j) {
                    if(!FloatEqual(a[i][j], b[i][j], epsilon)) {
                        return false;
                    }
                }
            }
            return true;
        }

        // 性能测试工具
        template<typename Func>
        static double MeasureExecutionTime(Func&& func) {
            auto start = std::chrono::high_resolution_clock::now();
            func();
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return duration.count() / 1000.0; // 返回毫秒
        }

        // 生成测试数据
        static std::vector<Math::Vec3> GenerateRandomVectors(size_t count, float min = -100.0f, float max = 100.0f) {
            std::vector<Math::Vec3> vectors;
            vectors.reserve(count);

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<float> dis(min, max);

            for(size_t i = 0; i < count; ++i) {
                vectors.emplace_back(dis(gen), dis(gen), dis(gen));
            }

            return vectors;
        }

        // 验证矩阵是否为单位矩阵
        static bool IsIdentityMatrix(const Math::Mat4& matrix, float epsilon = 1e-6f) {
            Math::Mat4 identity(1.0f);
            return Mat4Equal(matrix, identity, epsilon);
        }

        // 验证向量是否已归一化
        static bool IsNormalized(const Math::Vec3& vector, float epsilon = 1e-6f) {
            float len = Math::length(vector);
            return FloatEqual(len, 1.0f, epsilon);
        }

        // 生成测试用的变换矩阵
        static Math::Mat4 CreateTestTransform(const Math::Vec3& translation = Math::Vec3(0.0f),
                                        const Math::Vec3& rotation = Math::Vec3(0.0f),
                                        const Math::Vec3& scale_vec = Math::Vec3(1.0f)) {
           Math::Mat4 t = translate( Math::Mat4(1.0f), translation);
           Math::Mat4 rx = Math::rotate(Math::Mat4(1.0f),rotation.x, Math::Vec3(1.0f, 0.0f, 0.0f));
           Math::Mat4 ry = Math::rotate(Math::Mat4(1.0f),rotation.y, Math::Vec3(0.0f, 1.0f, 0.0f));
           Math::Mat4 rz = Math::rotate(Math::Mat4(1.0f),rotation.z, Math::Vec3(0.0f, 0.0f, 1.0f));
           Math::Mat4 s = Math::scale(Math::Mat4(1.0f),scale_vec);

            return t * rz * ry * rx * s;
        }

        // 验证数值范围
        template<typename T>
        static bool IsInRange(T value, T min, T max) {
            return value >= min && value <= max;
        }
    };

    // 自定义的gtest断言宏
#define EXPECT_VEC3_EQ(expected, actual) \
    EXPECT_TRUE(TestUtils::Vec3Equal(expected, actual)) \
    << "Expected: (" << expected.x << ", " << expected.y << ", " << expected.z << ")\n" \
    << "Actual: (" << actual.x << ", " << actual.y << ", " << actual.z << ")"

#define EXPECT_VEC2_EQ(expected, actual) \
    EXPECT_TRUE(TestUtils::Vec2Equal(expected, actual)) \
    << "Expected: (" << expected.x << ", " << expected.y << ")\n" \
    << "Actual: (" << actual.x << ", " << actual.y << ")"

#define EXPECT_MAT4_EQ(expected, actual) \
    EXPECT_TRUE(TestUtils::Mat4Equal(expected, actual)) \
    << "Matrices are not equal"

#define EXPECT_FLOAT_NEAR(expected, actual, epsilon) \
    EXPECT_TRUE(TestUtils::FloatEqual(expected, actual, epsilon)) \
    << "Expected: " << expected << "\nActual: " << actual << "\nEpsilon: " << epsilon

#define EXPECT_NORMALIZED(vector) \
    EXPECT_TRUE(TestUtils::IsNormalized(vector)) \
    << "Vector is not normalized: (" << vector.x << ", " << vector.y << ", " << vector.z << ")"

    // 性能测试宏
#define EXPECT_PERFORMANCE(code, max_time_ms) \
    do { \
        double elapsed = TestUtils::MeasureExecutionTime([&]() { code; }); \
        EXPECT_LT(elapsed, max_time_ms) \
        << "Code took " << elapsed << "ms, expected less than " << max_time_ms << "ms"; \
    } while(0)
} // namespace Comet::Tests
