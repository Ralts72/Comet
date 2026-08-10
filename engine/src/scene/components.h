#pragma once

#include "asset/handle.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"
#include "scene/entity_uuid.h"

#include <string>

namespace Comet {
    struct IdComponent {
        EntityId id = INVALID_ENTITY_ID;
    };

    struct UuidComponent {
        EntityUuid uuid;
    };

    struct NameComponent {
        std::string name = "Entity";
    };

    struct TransformComponent {
        Math::Vec3 translation = Math::Vec3(0.0f);
        // Euler angles in degrees.
        Math::Vec3 rotation = Math::Vec3(0.0f);
        Math::Vec3 scale = Math::Vec3(1.0f);

        void rotate(const Math::Vec3& delta) {
            rotation = Math::wrap_degrees(rotation + delta);
        }

        [[nodiscard]] Math::Mat4 to_matrix() const {
            return Math::compose_trs(translation, rotation, scale);
        }
    };

    struct RelationshipComponent {
        EntityId parent = INVALID_ENTITY_ID;
    };

    struct WorldTransformComponent {
        Math::Mat4 world_matrix = Math::Mat4(1.0f);
        Math::Mat4 camera_world_matrix = Math::Mat4(1.0f);
    };

    struct MeshRendererComponent {
        AssetHandle mesh;
        AssetHandle material;
    };

    struct CameraComponent {
        bool primary = false;
        // Vertical field of view in degrees.
        float fov = 45.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
    };
}
