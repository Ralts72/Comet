#pragma once
#include "core/math_utils.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <entt.hpp>

namespace Comet {
    using EntityId = std::uint64_t;

    struct IDComponent {
        EntityId id = 0;
    };

    struct NameComponent {
        std::string name = "Entity";
    };

    struct TransformComponent {
        Math::Vec3 translation = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Math::Vec3 scale = Math::Vec3(1.0f);
        Math::Mat4 world_matrix = Math::Mat4(1.0f);

        [[nodiscard]] Math::Mat4 get_local_matrix() const {
            Math::Mat4 matrix = Math::translate(Math::Mat4(1.0f), translation);
            matrix *= Math::to_mat4(rotation);
            matrix = Math::scale(matrix, scale);
            return matrix;
        }
    };

    struct RelationshipComponent {
        entt::entity parent = entt::null;
        std::vector<entt::entity> children;
    };

    struct MeshRendererComponent {
        MeshRendererComponent() = default;

        MeshRendererComponent(std::string mesh, std::string material)
            : mesh_name(std::move(mesh)), material_name(std::move(material)) {}

        std::string mesh_name;
        std::string material_name;
    };

    struct CameraComponent {
        float field_of_view = 45.0f;
        float near_clip = 0.1f;
        float far_clip = 100.0f;
        bool primary = true;
    };

    enum class LightType {
        Directional,
        Point,
        Spot,
    };

    struct LightComponent {
        explicit LightComponent(const LightType light_type = LightType::Directional)
            : type(light_type) {}

        LightType type = LightType::Directional;
        Math::Vec3 color = Math::Vec3(1.0f);
        float intensity = 1.0f;
    };
}
