#pragma once
#include <gtest/gtest.h>
#include "../engine/src/core/math_utils.h"

namespace Comet::Tests {
    class TestUtils {
    public:
        static bool FloatEqual(float a, float b, float epsilon = 1e-6f) {
            return std::abs(a - b) < epsilon;
        }

        static bool Vec3Equal(const Math::Vec3& a, const Math::Vec3& b, float epsilon = 1e-6f) {
            return FloatEqual(a.x, b.x, epsilon) &&
                   FloatEqual(a.y, b.y, epsilon) &&
                   FloatEqual(a.z, b.z, epsilon);
        }

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

        static bool IsIdentityMatrix(const Math::Mat4& matrix, float epsilon = 1e-6f) {
            Math::Mat4 identity(1.0f);
            return Mat4Equal(matrix, identity, epsilon);
        }
    };

#define EXPECT_VEC3_EQ(expected, actual) \
    EXPECT_TRUE(TestUtils::Vec3Equal(expected, actual)) \
    << "Expected: (" << expected.x << ", " << expected.y << ", " << expected.z << ")\n" \
    << "Actual: (" << actual.x << ", " << actual.y << ", " << actual.z << ")"

#define EXPECT_MAT4_EQ(expected, actual) \
    EXPECT_TRUE(TestUtils::Mat4Equal(expected, actual)) \
    << "Matrices are not equal"

} // namespace Comet::Tests
