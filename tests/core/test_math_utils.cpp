#include <gtest/gtest.h>

#include "core/math_utils.h"
#include "test_utils.h"

namespace Comet::Tests {

    TEST(MathUtilsTest, WrapsDegreesToSignedCycle) {
        EXPECT_FLOAT_EQ(Math::wrap_degrees(0.0f), 0.0f);
        EXPECT_FLOAT_EQ(Math::wrap_degrees(180.0f), -180.0f);
        EXPECT_FLOAT_EQ(Math::wrap_degrees(181.0f), -179.0f);
        EXPECT_FLOAT_EQ(Math::wrap_degrees(-181.0f), 179.0f);
        EXPECT_FLOAT_EQ(Math::wrap_degrees(540.0f), -180.0f);

        const Math::Vec3 wrapped =
            Math::wrap_degrees(Math::Vec3(360.0f, -450.0f, 721.0f));
        EXPECT_TRUE(TestUtils::Vec3Equal(wrapped, Math::Vec3(0.0f, -90.0f, 1.0f)));
    }

    TEST(MathUtilsTest, ComposeTrsUsesDegreeEulerAngles) {
        const Math::Mat4 matrix = Math::compose_trs(Math::Vec3(1.0f, 2.0f, 3.0f),
            Math::Vec3(0.0f, 0.0f, 90.0f), Math::Vec3(2.0f, 3.0f, 4.0f));

        const Math::Vec4 transformed = matrix * Math::Vec4(1.0f, 0.0f, 0.0f, 1.0f);

        EXPECT_TRUE(
            TestUtils::Vec3Equal(Math::Vec3(transformed), Math::Vec3(1.0f, 4.0f, 3.0f)));
    }

    TEST(MathUtilsTest, ComposeTrsAppliesEulerRotationsInXYZOrder) {
        const Math::Mat4 matrix = Math::compose_trs(
            Math::Vec3(0.0f), Math::Vec3(90.0f, 90.0f, 0.0f), Math::Vec3(1.0f));

        const Math::Vec4 transformed = matrix * Math::Vec4(0.0f, 1.0f, 0.0f, 0.0f);

        EXPECT_TRUE(
            TestUtils::Vec3Equal(Math::Vec3(transformed), Math::Vec3(1.0f, 0.0f, 0.0f)));
    }

    TEST(MathUtilsTest, ComposeTrsWrapsLargeEulerAngles) {
        const Math::Mat4 wrapped = Math::compose_trs(
            Math::Vec3(0.0f), Math::Vec3(10.0f, -20.0f, 30.0f), Math::Vec3(1.0f));
        const Math::Mat4 accumulated = Math::compose_trs(
            Math::Vec3(0.0f), Math::Vec3(3610.0f, -7220.0f, 10830.0f), Math::Vec3(1.0f));

        EXPECT_TRUE(TestUtils::Mat4Equal(wrapped, accumulated));
    }

    TEST(MathUtilsTest, PerspectiveUsesDegreeFov) {
        constexpr float fov_degrees = 45.0f;
        constexpr float aspect = 16.0f / 9.0f;
        constexpr float near_z = 0.1f;
        constexpr float far_z = 100.0f;

        const Math::Mat4 actual = Math::perspective(fov_degrees, aspect, near_z, far_z);
        const Math::Mat4 expected =
            glm::perspective(glm::radians(fov_degrees), aspect, near_z, far_z);

        EXPECT_TRUE(TestUtils::Mat4Equal(actual, expected));
    }

} // namespace Comet::Tests
