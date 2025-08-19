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
    struct Math {
        static constexpr float PI = 3.14159265358979323846f;
        static constexpr float DEG_TO_RAD = PI / 180.0f;
        static constexpr float RAD_TO_DEG = 180.0f / PI;

        using Vec2i = glm::ivec2;
        using Vec3i = glm::ivec3;
        using Vec4i = glm::ivec4;

        using Vec2 = glm::vec2;
        using Vec3 = glm::vec3;
        using Vec4 = glm::vec4;

        using Mat3 = glm::mat3;
        using Mat4 = glm::mat4;

        using Quat = glm::quat;

        template<typename T>
        static T identity() {
            return glm::identity<T>();
        }

        static float length(const Vec3& v) { return glm::length(v); }
        static Vec3 normalize(const Vec3& v) { return glm::normalize(v); }
        static float dot(const Vec3& a, const Vec3& b) { return glm::dot(a, b); }
        static Vec3 cross(const Vec3& a, const Vec3& b) { return glm::cross(a, b); }

        static float radians(float degrees) {
            return glm::radians(degrees);
        }

        static Mat4 translate(const Mat4& mat4, const Vec3& v) {
            return glm::translate(mat4, v);
        }

        static Mat4 scale(const Mat4& mat4, const Vec3& v) {
            return glm::scale(mat4, v);
        }

        static Mat4 rotate(const Mat4& mat4,float radians, const Vec3& axis) {
            return glm::rotate(mat4, radians, axis);
        }

        static Mat4 perspective(float fovY, float aspect, float nearZ, float farZ) {
            return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ);
        }

        static Mat4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
            return glm::ortho(left, right, bottom, top, nearZ, farZ);
        }

        static Quat angle_axis(float radians, const Vec3& axis) {
            return glm::angleAxis(radians, axis);
        }

        static Mat4 to_mat4(const Quat& q) {
            return glm::toMat4(q);
        }

        static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
            return glm::lookAt(eye, center, up);
        }

        static Mat4 transpose(const Mat4& m) {
            return glm::transpose(m);
        }

        static Mat4 inverse(const Mat4& m) {
            return glm::inverse(m);
        }

        struct Vertex {
            Vec3 position;
            Vec2 texcoord;
            Vec3 normal;
        };
    };
}
