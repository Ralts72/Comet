#pragma once
#include "common/export.h"

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/packing.hpp>

#include <array>
#include <cmath>
#include <optional>
#include <type_traits>

namespace Comet::Math {
    inline constexpr float PI = 3.14159265358979323846f;
    inline constexpr float DEG_TO_RAD = PI / 180.0f;
    inline constexpr float RAD_TO_DEG = 180.0f / PI;

    using Vec2i = glm::ivec2;
    using Vec3i = glm::ivec3;
    using Vec4i = glm::ivec4;

    using Vec2u = glm::uvec2;
    using Vec3u = glm::uvec3;
    using Vec4u = glm::uvec4;

    using Vec2 = glm::vec2;
    using Vec3 = glm::vec3;
    using Vec4 = glm::vec4;
    using Vec3d = glm::dvec3;
    using Vec4d = glm::dvec4;

    using Mat3 = glm::mat3;
    using Mat4 = glm::mat4;

    using Quat = glm::quat;

    // 数学类型与基础运算统一从这里使用；后端仍为 GLM，不另造一套数值实现。
    using glm::abs;
    using glm::ceil;
    using glm::clamp;
    using glm::cross;
    using glm::degrees;
    using glm::determinant;
    using glm::distance;
    using glm::dot;
    using glm::floor;
    using glm::fract;
    using glm::inverse;
    using glm::length;
    using glm::max;
    using glm::min;
    using glm::mix;
    using glm::normalize;
    using glm::sign;
    using glm::slerp;
    using glm::transpose;

    template<typename T> inline auto radians(const T& degrees) {
        if constexpr(std::is_integral_v<T>)
            return glm::radians(float(degrees));
        else
            return glm::radians(degrees);
    }

    template<typename Vector> [[nodiscard]] bool any_less(const Vector& a, const Vector& b) {
        return glm::any(glm::lessThan(a, b));
    }
    template<typename Vector> [[nodiscard]] bool any_less_equal(const Vector& a, const Vector& b) {
        return glm::any(glm::lessThanEqual(a, b));
    }
    template<typename Vector> [[nodiscard]] bool any_greater(const Vector& a, const Vector& b) {
        return glm::any(glm::greaterThan(a, b));
    }
    template<typename Vector>
    [[nodiscard]] bool any_greater_equal(const Vector& a, const Vector& b) {
        return glm::any(glm::greaterThanEqual(a, b));
    }
    template<typename Vector> [[nodiscard]] bool all_less(const Vector& a, const Vector& b) {
        return glm::all(glm::lessThan(a, b));
    }
    template<typename Vector> [[nodiscard]] bool all_less_equal(const Vector& a, const Vector& b) {
        return glm::all(glm::lessThanEqual(a, b));
    }
    template<typename Vector> [[nodiscard]] bool all_greater(const Vector& a, const Vector& b) {
        return glm::all(glm::greaterThan(a, b));
    }
    template<typename Vector>
    [[nodiscard]] bool all_greater_equal(const Vector& a, const Vector& b) {
        return glm::all(glm::greaterThanEqual(a, b));
    }

    using Half4 = std::array<uint16_t, 4>;
    [[nodiscard]] inline uint16_t pack_half(float value) {
        return glm::packHalf1x16(value);
    }
    [[nodiscard]] inline float unpack_half(uint16_t value) {
        return glm::unpackHalf1x16(value);
    }
    [[nodiscard]] inline Half4 pack_half(const Vec4& value) {
        const auto packed = glm::packHalf(value);
        return {packed.x, packed.y, packed.z, packed.w};
    }
    [[nodiscard]] inline Vec4 unpack_half(const Half4& value) {
        return glm::unpackHalf(glm::u16vec4(value[0], value[1], value[2], value[3]));
    }

    [[nodiscard]] inline bool is_finite(const Vec2& value) {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    [[nodiscard]] inline bool is_finite(const Vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    [[nodiscard]] inline bool is_finite(const Vec4& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
               && std::isfinite(value.w);
    }

    [[nodiscard]] inline bool is_finite(const Mat4& value) {
        return is_finite(value[0]) && is_finite(value[1]) && is_finite(value[2])
               && is_finite(value[3]);
    }

    template<typename T> T identity() {
        return glm::identity<T>();
    }

    [[nodiscard]] inline std::optional<Vec3> normalized_direction(const Vec3& value) {
        if(!is_finite(value))
            return std::nullopt;
        const auto magnitude = glm::abs(value);
        const float largest = glm::max(magnitude.x, glm::max(magnitude.y, magnitude.z));
        if(largest == 0)
            return std::nullopt;
        // 先缩放，避免有限的极大／极小方向在求长度时溢出或下溢。
        return glm::normalize(value / largest);
    }
    [[nodiscard]] inline std::optional<Vec3> perpendicular_direction(
        const Vec3& axis, const Vec3& reference) {
        const auto direction = normalized_direction(axis);
        const auto normal = normalized_direction(reference);
        if(!direction || !normal)
            return std::nullopt;
        const auto tangent = normalized_direction(glm::cross(*direction, *normal));
        if(!tangent)
            return std::nullopt;
        return glm::cross(*tangent, *direction);
    }

    // 列向量依次为右、上、后；forward 沿局部 -Z，保留 up 指定的滚转方向。
    [[nodiscard]] inline std::optional<Mat3> view_basis(
        const Vec3& forward, const Vec3& up, const float minimum_cross_length = 0) {
        const auto direction = normalized_direction(forward);
        const auto reference = normalized_direction(up);
        if(!direction || !reference)
            return std::nullopt;
        const auto cross = glm::cross(*direction, *reference);
        if(glm::length(cross) < minimum_cross_length)
            return std::nullopt;
        const auto right = normalized_direction(cross);
        if(!right)
            return std::nullopt;
        return Mat3(*right, glm::cross(*right, *direction), -*direction);
    }

    // 保留模拟输入幅度，只限制超出单位长度的向量；double 避免有限大值溢出。
    template<glm::length_t Size>
    [[nodiscard]] inline glm::vec<Size, float> clamp_unit_length(
        const glm::vec<Size, float>& value) {
        const glm::vec<Size, double> precise(value);
        const double squared_length = glm::dot(precise, precise);
        if(squared_length <= 1)
            return value;
        return glm::vec<Size, float>(precise / std::sqrt(squared_length));
    }

    inline float wrap_degrees(const float degrees) {
        if(!std::isfinite(degrees)) {
            return degrees;
        }

        float wrapped = std::fmod(degrees, 360.0f);
        if(wrapped >= 180.0f) {
            wrapped -= 360.0f;
        } else if(wrapped < -180.0f) {
            wrapped += 360.0f;
        }
        return wrapped;
    }

    inline Vec3 wrap_degrees(const Vec3& degrees) {
        return {wrap_degrees(degrees.x), wrap_degrees(degrees.y), wrap_degrees(degrees.z)};
    }

    inline Mat4 translate(const Mat4& mat4, const Vec3& v) {
        return glm::translate(mat4, v);
    }

    inline Mat4 scale(const Mat4& mat4, const Vec3& v) {
        return glm::scale(mat4, v);
    }

    inline Mat4 rotate(const Mat4& mat4, float radians, const Vec3& axis) {
        return glm::rotate(mat4, radians, axis);
    }

    inline Mat4 compose_trs(
        const Vec3& translation, const Vec3& rotation_degrees, const Vec3& scale_vector) {
        // 欧拉旋转按 X/Y/Z 顺序应用：T * Rz * Ry * Rx * S。
        const Vec3 wrapped_rotation = wrap_degrees(rotation_degrees);
        Mat4 matrix = translate(Mat4(1.0f), translation);
        matrix = rotate(matrix, radians(wrapped_rotation.z), Vec3(0.0f, 0.0f, 1.0f));
        matrix = rotate(matrix, radians(wrapped_rotation.y), Vec3(0.0f, 1.0f, 0.0f));
        matrix = rotate(matrix, radians(wrapped_rotation.x), Vec3(1.0f, 0.0f, 0.0f));
        return scale(matrix, scale_vector);
    }

    // 正 TRS 仿射变换的各轴缩放；拒绝剪切、镜像和退化。
    [[nodiscard]] COMET_API std::optional<Vec3> positive_trs_scale(const Mat4& matrix);
    // 正等比仿射变换的缩放，供角色与骨骼空间转换共用。
    [[nodiscard]] COMET_API std::optional<float> uniform_scale(const Mat4& matrix);
    // 提取平移与旋转；矩阵不可分解时返回空。
    [[nodiscard]] COMET_API std::optional<Mat4> without_scale(const Mat4& matrix);
    struct TransformDecomposition {
        Vec3 translation;
        Quat rotation;
        Vec3 scale;
        Vec3 skew;
        Vec4 perspective;
    };
    [[nodiscard]] COMET_API std::optional<TransformDecomposition> decompose(const Mat4& matrix);

    inline Mat4 perspective(float fov_degrees, float aspect, float nearZ, float farZ) {
        return glm::perspective(glm::radians(fov_degrees), aspect, nearZ, farZ);
    }

    inline Mat4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
        return glm::ortho(left, right, bottom, top, nearZ, farZ);
    }

    inline Quat angle_axis(float radians, const Vec3& axis) {
        return glm::angleAxis(radians, axis);
    }

    inline Mat4 to_mat4(const Quat& q) {
        return glm::toMat4(q);
    }
    inline Quat to_quat(const Mat3& matrix) {
        return glm::quat_cast(matrix);
    }
    inline Quat to_quat(const Mat4& matrix) {
        return glm::quat_cast(matrix);
    }
    inline Vec3 euler_angles(const Quat& rotation) {
        return glm::eulerAngles(rotation);
    }
    // from／to 均为单位方向；角度接口使用弧度。
    inline Quat rotation_between(const Vec3& from, const Vec3& to) {
        return glm::rotation(from, to);
    }
    inline Mat4 affine_inverse(const Mat4& matrix) {
        return glm::affineInverse(matrix);
    }

    inline Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
        return glm::lookAt(eye, center, up);
    }

}
