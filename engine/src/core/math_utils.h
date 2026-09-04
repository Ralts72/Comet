#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>

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

    using Mat3 = glm::mat3;
    using Mat4 = glm::mat4;

    using Quat = glm::quat;

    template<typename T> T identity() {
        return glm::identity<T>();
    }

    inline float length(const Vec3& v) {
        return glm::length(v);
    }
    inline Vec3 normalize(const Vec3& v) {
        return glm::normalize(v);
    }
    inline float dot(const Vec3& a, const Vec3& b) {
        return glm::dot(a, b);
    }
    inline Vec3 cross(const Vec3& a, const Vec3& b) {
        return glm::cross(a, b);
    }

    inline float radians(float degrees) {
        return glm::radians(degrees);
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
        return {
            wrap_degrees(degrees.x), wrap_degrees(degrees.y), wrap_degrees(degrees.z)};
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
        // Euler rotations are applied in X/Y/Z order: T * Rz * Ry * Rx * S.
        const Vec3 wrapped_rotation = wrap_degrees(rotation_degrees);
        Mat4 matrix = translate(Mat4(1.0f), translation);
        matrix = rotate(matrix, radians(wrapped_rotation.z), Vec3(0.0f, 0.0f, 1.0f));
        matrix = rotate(matrix, radians(wrapped_rotation.y), Vec3(0.0f, 1.0f, 0.0f));
        matrix = rotate(matrix, radians(wrapped_rotation.x), Vec3(1.0f, 0.0f, 0.0f));
        return scale(matrix, scale_vector);
    }

    inline Mat4 perspective(float fov_degrees, float aspect, float nearZ, float farZ) {
        return glm::perspective(glm::radians(fov_degrees), aspect, nearZ, farZ);
    }

    inline Mat4 ortho(
        float left, float right, float bottom, float top, float nearZ, float farZ) {
        return glm::ortho(left, right, bottom, top, nearZ, farZ);
    }

    inline Quat angle_axis(float radians, const Vec3& axis) {
        return glm::angleAxis(radians, axis);
    }

    inline Mat4 to_mat4(const Quat& q) {
        return glm::toMat4(q);
    }

    inline Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
        return glm::lookAt(eye, center, up);
    }

    inline Mat4 transpose(const Mat4& m) {
        return glm::transpose(m);
    }

    inline Mat4 inverse(const Mat4& m) {
        return glm::inverse(m);
    }

}
