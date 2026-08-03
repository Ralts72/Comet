#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"

#include <string>

namespace Comet {
    struct IdComponent {
        EntityId id = INVALID_ENTITY_ID;
    };

    struct NameComponent {
        std::string name = "Entity";
    };

    struct TransformComponent {
        Math::Vec3 translation = Math::Vec3(0.0f);
        // Euler angles in degrees. The local matrix is T * Rz * Ry * Rx * S.
        Math::Vec3 rotation = Math::Vec3(0.0f);
        Math::Vec3 scale = Math::Vec3(1.0f);

        [[nodiscard]] Math::Mat4 local_matrix() const {
            Math::Mat4 matrix = Math::translate(Math::Mat4(1.0f), translation);
            matrix = Math::rotate(
                matrix, Math::radians(rotation.z), Math::Vec3(0.0f, 0.0f, 1.0f));
            matrix = Math::rotate(
                matrix, Math::radians(rotation.y), Math::Vec3(0.0f, 1.0f, 0.0f));
            matrix = Math::rotate(
                matrix, Math::radians(rotation.x), Math::Vec3(1.0f, 0.0f, 0.0f));
            return Math::scale(matrix, scale);
        }
    };

    struct MeshRendererComponent {
        AssetHandle mesh;
        AssetHandle material;
    };

    struct CameraComponent {
        bool primary = false;
        float fov = 45.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
    };
}
