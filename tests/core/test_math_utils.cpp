#include <gtest/gtest.h>

#include "core/math_utils.h"
#include "support/math_assertions.h"

#include <limits>

namespace Comet::Tests {

    TEST(MathUtilsTest, ViewBasisUsesNegativeZForwardAndPreservesRoll) {
        const auto basis = Math::view_basis({0, 0, -5}, {0, 3, 0});
        ASSERT_TRUE(basis);
        EXPECT_TRUE(TestUtils::Mat4Equal(Math::Mat4(*basis), Math::Mat4(1)));
        const auto rolled = Math::view_basis({0, 0, -1}, {-1, 0, 0});
        ASSERT_TRUE(rolled);
        EXPECT_TRUE(TestUtils::Vec3Equal((*rolled)[0], {0, 1, 0}));
        EXPECT_TRUE(TestUtils::Vec3Equal((*rolled)[1], {-1, 0, 0}));
        EXPECT_FALSE(Math::view_basis({0, 0, 0}, {0, 1, 0}));
        EXPECT_FALSE(Math::view_basis({0, 1, 0}, {0, 1, 0}));
        EXPECT_FALSE(Math::view_basis({0, 1, -0.000001f}, {0, 1, 0}, 0.00001f));
    }

    TEST(MathUtilsTest, UnitLengthClampPreservesAnalogInputAndHandlesLargeDirections) {
        EXPECT_EQ(Math::clamp_unit_length(Math::Vec2(0)), Math::Vec2(0));
        EXPECT_EQ(Math::clamp_unit_length(Math::Vec2(0.3f, 0.4f)), Math::Vec2(0.3f, 0.4f));
        EXPECT_TRUE(
            TestUtils::Vec3Equal(Math::clamp_unit_length(Math::Vec3(3, 4, 0)), {0.6f, 0.8f, 0}));
        const float large = std::numeric_limits<float>::max();
        const auto planar = Math::clamp_unit_length(Math::Vec2(large, -large));
        EXPECT_TRUE(Math::is_finite(planar));
        EXPECT_NEAR(glm::length(planar), 1, 0.00001f);
        EXPECT_NEAR(planar.x, -planar.y, 0.00001f);
        const auto spatial = Math::clamp_unit_length(Math::Vec3(large));
        EXPECT_TRUE(Math::is_finite(spatial));
        EXPECT_NEAR(Math::length(spatial), 1, 0.00001f);
    }

    TEST(MathUtilsTest, ChecksEveryVectorComponentForFiniteValues) {
        const auto check = []<typename Vector>() {
            EXPECT_TRUE(Math::is_finite(Vector(0.0f)));
            EXPECT_TRUE(Math::is_finite(Vector(std::numeric_limits<float>::max())));
            EXPECT_TRUE(Math::is_finite(Vector(-std::numeric_limits<float>::max())));
            for(int component = 0; component < Vector::length(); ++component) {
                for(const float invalid : {std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
                    Vector value(1.0f);
                    value[component] = invalid;
                    EXPECT_FALSE(Math::is_finite(value));
                }
            }
        };
        check.operator()<Math::Vec2>();
        check.operator()<Math::Vec3>();
        check.operator()<Math::Vec4>();
    }

    TEST(MathUtilsTest, WrapsDegreesToSignedCycle) {
        EXPECT_FLOAT_EQ(Math::wrap_degrees(0.0f), 0.0f);
        EXPECT_FLOAT_EQ(Math::wrap_degrees(180.0f), -180.0f);
        EXPECT_FLOAT_EQ(Math::wrap_degrees(181.0f), -179.0f);
        EXPECT_FLOAT_EQ(Math::wrap_degrees(-181.0f), 179.0f);
        EXPECT_FLOAT_EQ(Math::wrap_degrees(540.0f), -180.0f);

        const Math::Vec3 wrapped = Math::wrap_degrees(Math::Vec3(360.0f, -450.0f, 721.0f));
        EXPECT_TRUE(TestUtils::Vec3Equal(wrapped, Math::Vec3(0.0f, -90.0f, 1.0f)));
    }

    TEST(MathUtilsTest, ComposeTrsUsesDegreeEulerAngles) {
        const Math::Mat4 matrix = Math::compose_trs(Math::Vec3(1.0f, 2.0f, 3.0f),
            Math::Vec3(0.0f, 0.0f, 90.0f), Math::Vec3(2.0f, 3.0f, 4.0f));

        const Math::Vec4 transformed = matrix * Math::Vec4(1.0f, 0.0f, 0.0f, 1.0f);

        EXPECT_TRUE(TestUtils::Vec3Equal(Math::Vec3(transformed), Math::Vec3(1.0f, 4.0f, 3.0f)));
    }

    TEST(MathUtilsTest, ComposeTrsAppliesEulerRotationsInXYZOrder) {
        const Math::Mat4 matrix =
            Math::compose_trs(Math::Vec3(0.0f), Math::Vec3(90.0f, 90.0f, 0.0f), Math::Vec3(1.0f));

        const Math::Vec4 transformed = matrix * Math::Vec4(0.0f, 1.0f, 0.0f, 0.0f);

        EXPECT_TRUE(TestUtils::Vec3Equal(Math::Vec3(transformed), Math::Vec3(1.0f, 0.0f, 0.0f)));
    }

    TEST(MathUtilsTest, ComposeTrsWrapsLargeEulerAngles) {
        const Math::Mat4 wrapped =
            Math::compose_trs(Math::Vec3(0.0f), Math::Vec3(10.0f, -20.0f, 30.0f), Math::Vec3(1.0f));
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

}
