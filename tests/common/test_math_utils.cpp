#include <gtest/gtest.h>
#include "common/math_utils.h"
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

    bool vec3Equal(const Vec3& a, const Vec3& b) {
        return floatEqual(a.x, b.x) && floatEqual(a.y, b.y) && floatEqual(a.z, b.z);
    }
};

TEST_F(MathUtilsTest, Constants) {
    EXPECT_FLOAT_EQ(PI, 3.14159265358979323846f);
    EXPECT_FLOAT_EQ(DEG_TO_RAD, PI / 180.0f);
    EXPECT_FLOAT_EQ(RAD_TO_DEG, 180.0f / PI);
}

TEST_F(MathUtilsTest, VectorLength) {
    Vec3 v1(1.0f, 0.0f, 0.0f);
    Vec3 v2(3.0f, 4.0f, 0.0f);
    Vec3 v3(1.0f, 1.0f, 1.0f);

    EXPECT_FLOAT_EQ(length(v1), 1.0f);
    EXPECT_FLOAT_EQ(length(v2), 5.0f);
    EXPECT_TRUE(floatEqual(length(v3), std::sqrt(3.0f)));
}

TEST_F(MathUtilsTest, VectorNormalization) {
    Vec3 v1(3.0f, 4.0f, 0.0f);
    Vec3 normalized = normalize(v1);

    EXPECT_TRUE(floatEqual(length(normalized), 1.0f));
    EXPECT_TRUE(vec3Equal(normalized, Vec3(0.6f, 0.8f, 0.0f)));
}

TEST_F(MathUtilsTest, DotProduct) {
    Vec3 v1(1.0f, 0.0f, 0.0f);
    Vec3 v2(0.0f, 1.0f, 0.0f);
    Vec3 v3(1.0f, 1.0f, 0.0f);

    EXPECT_FLOAT_EQ(dot(v1, v2), 0.0f);
    EXPECT_FLOAT_EQ(dot(v1, v3), 1.0f);
    EXPECT_FLOAT_EQ(dot(v1, v1), 1.0f);
}

TEST_F(MathUtilsTest, CrossProduct) {
    Vec3 x(1.0f, 0.0f, 0.0f);
    Vec3 y(0.0f, 1.0f, 0.0f);
    Vec3 z(0.0f, 0.0f, 1.0f);

    Vec3 result = cross(x, y);
    EXPECT_TRUE(vec3Equal(result, z));

    result = cross(y, z);
    EXPECT_TRUE(vec3Equal(result, x));

    result = cross(z, x);
    EXPECT_TRUE(vec3Equal(result, y));
}

TEST_F(MathUtilsTest, TransformationMatrices) {
    // 测试平移矩阵
    Vec3 translation(1.0f, 2.0f, 3.0f);
    Mat4 translateMat = translate(translation);
    Vec4 point(0.0f, 0.0f, 0.0f, 1.0f);
    Vec4 translatedPoint = translateMat * point;

    EXPECT_FLOAT_EQ(translatedPoint.x, 1.0f);
    EXPECT_FLOAT_EQ(translatedPoint.y, 2.0f);
    EXPECT_FLOAT_EQ(translatedPoint.z, 3.0f);
    EXPECT_FLOAT_EQ(translatedPoint.w, 1.0f);

    // 测试缩放矩阵
    Vec3 scaleVec(2.0f, 3.0f, 4.0f);
    Mat4 scaleMat = scale(scaleVec);
    Vec4 scaledPoint = scaleMat * Vec4(1.0f, 1.0f, 1.0f, 1.0f);

    EXPECT_FLOAT_EQ(scaledPoint.x, 2.0f);
    EXPECT_FLOAT_EQ(scaledPoint.y, 3.0f);
    EXPECT_FLOAT_EQ(scaledPoint.z, 4.0f);
    EXPECT_FLOAT_EQ(scaledPoint.w, 1.0f);
}

TEST_F(MathUtilsTest, RotationMatrix) {
    Vec3 axis(0.0f, 0.0f, 1.0f);
    float angle = PI / 2.0f; // 90度
    Mat4 rotMat = rotate(angle, axis);

    Vec4 point(1.0f, 0.0f, 0.0f, 1.0f);
    Vec4 rotatedPoint = rotMat * point;

    EXPECT_TRUE(floatEqual(rotatedPoint.x, 0.0f));
    EXPECT_TRUE(floatEqual(rotatedPoint.y, 1.0f));
    EXPECT_TRUE(floatEqual(rotatedPoint.z, 0.0f));
    EXPECT_FLOAT_EQ(rotatedPoint.w, 1.0f);
}

TEST_F(MathUtilsTest, ProjectionMatrices) {
    // 测试透视投影矩阵
    float fov = 45.0f;
    float aspect = 16.0f / 9.0f;
    float nearZ = 0.1f;
    float farZ = 100.0f;

    Mat4 perspMat = perspective(fov, aspect, nearZ, farZ);
    EXPECT_FALSE(perspMat == Mat4(0.0f)); // 确保矩阵不为零

    // 测试正交投影矩阵
    Mat4 orthoMat = ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);
    EXPECT_FALSE(orthoMat == Mat4(0.0f)); // 确保矩阵不为零
}

TEST_F(MathUtilsTest, QuaternionAngleAxis) {
    Vec3 axis(0.0f, 0.0f, 1.0f);
    float angle = PI / 2.0f;
    Quat q = angle_axis(angle, axis);

    // 验证四元数的长度为1
    float quatLength = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    EXPECT_TRUE(floatEqual(quatLength, 1.0f));
}

TEST_F(MathUtilsTest, VectorTypes) {
    // 测试不同的向量类型是否正确定义
    Vec2i v2i(1, 2);
    Vec3i v3i(1, 2, 3);
    Vec4i v4i(1, 2, 3, 4);

    Vec2 v2(1.0f, 2.0f);
    Vec3 v3(1.0f, 2.0f, 3.0f);
    Vec4 v4(1.0f, 2.0f, 3.0f, 4.0f);

    EXPECT_EQ(v2i.x, 1);
    EXPECT_EQ(v2i.y, 2);

    EXPECT_FLOAT_EQ(v2.x, 1.0f);
    EXPECT_FLOAT_EQ(v2.y, 2.0f);

    EXPECT_FLOAT_EQ(v3.x, 1.0f);
    EXPECT_FLOAT_EQ(v3.y, 2.0f);
    EXPECT_FLOAT_EQ(v3.z, 3.0f);
}

} // namespace Comet::Tests
