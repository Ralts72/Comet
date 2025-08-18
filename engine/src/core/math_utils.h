#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Comet {
    constexpr float PI = 3.14159265358979323846f;
    constexpr float DEG_TO_RAD = PI / 180.0f;
    constexpr float RAD_TO_DEG = 180.0f / PI;

    using Vec2i = glm::ivec2;
    using Vec3i = glm::ivec3;
    using Vec4i = glm::ivec4;

    using Vec2 = glm::vec2;
    using Vec3 = glm::vec3;
    using Vec4 = glm::vec4;

    using Mat3 = glm::mat3;
    using Mat4 = glm::mat4;

    using Quat = glm::quat;

    inline float length(const Vec3& v) { return glm::length(v); }
    inline Vec3 normalize(const Vec3& v) { return glm::normalize(v); }
    inline float dot(const Vec3& a, const Vec3& b) { return glm::dot(a, b); }
    inline Vec3 cross(const Vec3& a, const Vec3& b) { return glm::cross(a, b); }

    inline Mat4 translate(const Vec3& v) {
        return glm::translate(Mat4(1.0f), v);
    }

    inline Mat4 scale(const Vec3& v) {
        return glm::scale(Mat4(1.0f), v);
    }

    inline Mat4 rotate(float radians, const Vec3& axis) {
        return glm::rotate(Mat4(1.0f), radians, axis);
    }

    inline Mat4 perspective(float fovY, float aspect, float nearZ, float farZ) {
        return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ);
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

    inline Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
        return glm::lookAt(eye, center, up);
    }

    inline Mat4 transpose(const Mat4& m) {
        return glm::transpose(m);
    }

    inline Mat4 inverse(const Mat4& m) {
        return glm::inverse(m);
    }

    struct Vertex {
        Vec3 position;
        Vec2 texcoord;
        Vec3 normal;
    };
}
