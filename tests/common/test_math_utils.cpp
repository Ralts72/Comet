#include <gtest/gtest.h>
#include "../../engine/src/core/math_utils.h"
#include <cmath>

namespace Comet::Tests {

class MathUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}

    static constexpr float EPSILON = 1e-6f;

    bool floatEqual(float a, float b) {
        return std::abs(a - b) < EPSILON;
    }

    bool vec3Equal(const Math::Vec3& a, const Math::Vec3& b) {
        return floatEqual(a.x, b.x) && floatEqual(a.y, b.y) && floatEqual(a.z, b.z);
    }

    bool mat4Equal(const Math::Mat4& a, const Math::Mat4& b) {
        for(int column = 0; column < 4; ++column) {
            for(int row = 0; row < 4; ++row) {
                if(!floatEqual(a[column][row], b[column][row])) {
                    return false;
                }
            }
        }
        return true;
    }
};

TEST_F(MathUtilsTest, Constants) {
    EXPECT_FLOAT_EQ(Math::PI, 3.14159265358979323846f);
    EXPECT_FLOAT_EQ(Math::DEG_TO_RAD, Math::PI / 180.0f);
    EXPECT_FLOAT_EQ(Math::RAD_TO_DEG, 180.0f / Math::PI);
}

TEST_F(MathUtilsTest, WrapsDegreesToSignedCycle) {
    EXPECT_FLOAT_EQ(Math::wrap_degrees(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(Math::wrap_degrees(180.0f), -180.0f);
    EXPECT_FLOAT_EQ(Math::wrap_degrees(181.0f), -179.0f);
    EXPECT_FLOAT_EQ(Math::wrap_degrees(-181.0f), 179.0f);
    EXPECT_FLOAT_EQ(Math::wrap_degrees(540.0f), -180.0f);

    const Math::Vec3 wrapped = Math::wrap_degrees(
        Math::Vec3(360.0f, -450.0f, 721.0f));
    EXPECT_TRUE(vec3Equal(wrapped, Math::Vec3(0.0f, -90.0f, 1.0f)));
}

TEST_F(MathUtilsTest, VectorLength) {
    Math::Vec3 v1(1.0f, 0.0f, 0.0f);
    Math::Vec3 v2(3.0f, 4.0f, 0.0f);
    Math::Vec3 v3(1.0f, 1.0f, 1.0f);

    EXPECT_FLOAT_EQ(Math::length(v1), 1.0f);
    EXPECT_FLOAT_EQ(Math::length(v2), 5.0f);
    EXPECT_TRUE(floatEqual(Math::length(v3), std::sqrt(3.0f)));
}

TEST_F(MathUtilsTest, VectorNormalization) {
    Math::Vec3 v1(3.0f, 4.0f, 0.0f);
    Math::Vec3 normalized = normalize(v1);

    EXPECT_TRUE(floatEqual(Math::length(normalized), 1.0f));
    EXPECT_TRUE(vec3Equal(normalized, Math::Vec3(0.6f, 0.8f, 0.0f)));
}

TEST_F(MathUtilsTest, DotProduct) {
    Math::Vec3 v1(1.0f, 0.0f, 0.0f);
    Math::Vec3 v2(0.0f, 1.0f, 0.0f);
    Math::Vec3 v3(1.0f, 1.0f, 0.0f);

    EXPECT_FLOAT_EQ(Math::dot(v1, v2), 0.0f);
    EXPECT_FLOAT_EQ(Math::dot(v1, v3), 1.0f);
    EXPECT_FLOAT_EQ(Math::dot(v1, v1), 1.0f);
}

TEST_F(MathUtilsTest, CrossProduct) {
    Math::Vec3 x(1.0f, 0.0f, 0.0f);
    Math::Vec3 y(0.0f, 1.0f, 0.0f);
    Math::Vec3 z(0.0f, 0.0f, 1.0f);

    Math::Vec3 result = Math::cross(x, y);
    EXPECT_TRUE(vec3Equal(result, z));

    result = cross(y, z);
    EXPECT_TRUE(vec3Equal(result, x));

    result = cross(z, x);
    EXPECT_TRUE(vec3Equal(result, y));
}

TEST_F(MathUtilsTest, TransformationMatrices) {
    // 测试平移矩阵
    Math::Vec3 translation(1.0f, 2.0f, 3.0f);
    Math::Mat4 translateMat = Math::translate(Math::Mat4(1.0f),translation);
    Math::Vec4 point(0.0f, 0.0f, 0.0f, 1.0f);
    Math::Vec4 translatedPoint = translateMat * point;

    EXPECT_FLOAT_EQ(translatedPoint.x, 1.0f);
    EXPECT_FLOAT_EQ(translatedPoint.y, 2.0f);
    EXPECT_FLOAT_EQ(translatedPoint.z, 3.0f);
    EXPECT_FLOAT_EQ(translatedPoint.w, 1.0f);

    // 测试缩放矩阵
    Math::Vec3 scaleVec(2.0f, 3.0f, 4.0f);
    Math::Mat4 scaleMat = Math::scale(Math::Mat4(1.0f), scaleVec);
    Math::Vec4 scaledPoint = scaleMat * Math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);

    EXPECT_FLOAT_EQ(scaledPoint.x, 2.0f);
    EXPECT_FLOAT_EQ(scaledPoint.y, 3.0f);
    EXPECT_FLOAT_EQ(scaledPoint.z, 4.0f);
    EXPECT_FLOAT_EQ(scaledPoint.w, 1.0f);
}

TEST_F(MathUtilsTest, RotationMatrix) {
    Math::Vec3 axis(0.0f, 0.0f, 1.0f);
    float angle = Math::PI / 2.0f; // 90度
    Math::Mat4 rotMat = Math::rotate(Math::Mat4(1.0f), angle, axis);

    Math::Vec4 point(1.0f, 0.0f, 0.0f, 1.0f);
    Math::Vec4 rotatedPoint = rotMat * point;

    EXPECT_TRUE(floatEqual(rotatedPoint.x, 0.0f));
    EXPECT_TRUE(floatEqual(rotatedPoint.y, 1.0f));
    EXPECT_TRUE(floatEqual(rotatedPoint.z, 0.0f));
    EXPECT_FLOAT_EQ(rotatedPoint.w, 1.0f);
}

TEST_F(MathUtilsTest, ComposeTrsUsesDegreeEulerAngles) {
    const Math::Mat4 matrix = Math::compose_trs(
        Math::Vec3(1.0f, 2.0f, 3.0f),
        Math::Vec3(0.0f, 0.0f, 90.0f),
        Math::Vec3(2.0f, 3.0f, 4.0f));

    const Math::Vec4 transformed =
        matrix * Math::Vec4(1.0f, 0.0f, 0.0f, 1.0f);

    EXPECT_TRUE(vec3Equal(
        Math::Vec3(transformed), Math::Vec3(1.0f, 4.0f, 3.0f)));
}

TEST_F(MathUtilsTest, ComposeTrsAppliesEulerRotationsInXYZOrder) {
    const Math::Mat4 matrix = Math::compose_trs(
        Math::Vec3(0.0f),
        Math::Vec3(90.0f, 90.0f, 0.0f),
        Math::Vec3(1.0f));

    const Math::Vec4 transformed =
        matrix * Math::Vec4(0.0f, 1.0f, 0.0f, 0.0f);

    EXPECT_TRUE(vec3Equal(
        Math::Vec3(transformed), Math::Vec3(1.0f, 0.0f, 0.0f)));
}

TEST_F(MathUtilsTest, ComposeTrsWrapsLargeEulerAngles) {
    const Math::Mat4 wrapped = Math::compose_trs(
        Math::Vec3(0.0f),
        Math::Vec3(10.0f, -20.0f, 30.0f),
        Math::Vec3(1.0f));
    const Math::Mat4 accumulated = Math::compose_trs(
        Math::Vec3(0.0f),
        Math::Vec3(3610.0f, -7220.0f, 10830.0f),
        Math::Vec3(1.0f));

    EXPECT_TRUE(mat4Equal(wrapped, accumulated));
}

TEST_F(MathUtilsTest, ProjectionMatrices) {
    // 测试透视投影矩阵
    float fov_degrees = 45.0f;
    float aspect = 16.0f / 9.0f;
    float nearZ = 0.1f;
    float farZ = 100.0f;

    Math::Mat4 perspMat = Math::perspective(fov_degrees, aspect, nearZ, farZ);
    Math::Mat4 expectedPerspMat = glm::perspective(glm::radians(fov_degrees), aspect, nearZ, farZ);
    EXPECT_FALSE(perspMat == Math::Mat4(0.0f)); // 确保矩阵不为零
    EXPECT_TRUE(perspMat == expectedPerspMat); // FOV 接口统一使用角度

    // 测试正交投影矩阵
    Math::Mat4 orthoMat = Math::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);
    EXPECT_FALSE(orthoMat == Math::Mat4(0.0f)); // 确保矩阵不为零
}

TEST_F(MathUtilsTest, QuaternionAngleAxis) {
    Math::Vec3 axis(0.0f, 0.0f, 1.0f);
    float angle = Math::PI / 2.0f;
    Math::Quat q = Math::angle_axis(angle, axis);

    // 验证四元数的长度为1
    float quatLength = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    EXPECT_TRUE(floatEqual(quatLength, 1.0f));
}

TEST_F(MathUtilsTest, VectorTypes) {
    // 测试不同的向量类型是否正确定义
    Math::Vec2i v2i(1, 2);
    Math::Vec3i v3i(1, 2, 3);
    Math::Vec4i v4i(1, 2, 3, 4);

    Math::Vec2 v2(1.0f, 2.0f);
    Math::Vec3 v3(1.0f, 2.0f, 3.0f);
    Math::Vec4 v4(1.0f, 2.0f, 3.0f, 4.0f);

    EXPECT_EQ(v2i.x, 1);
    EXPECT_EQ(v2i.y, 2);

    EXPECT_FLOAT_EQ(v2.x, 1.0f);
    EXPECT_FLOAT_EQ(v2.y, 2.0f);

    EXPECT_FLOAT_EQ(v3.x, 1.0f);
    EXPECT_FLOAT_EQ(v3.y, 2.0f);
    EXPECT_FLOAT_EQ(v3.z, 3.0f);
}

} // namespace Comet::Tests
