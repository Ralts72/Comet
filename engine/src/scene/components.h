#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"
#include "scene/entity_uuid.h"

#include <string>
#include <type_traits>

namespace Comet {
    struct COMET_API IdComponent {
        EntityId id = INVALID_ENTITY_ID;
    };

    struct COMET_API UuidComponent {
        EntityUuid uuid;
    };

    struct COMET_API NameComponent {
        std::string name = "Entity";
    };

    struct COMET_API TransformComponent {
        Math::Vec3 translation = Math::Vec3(0.0f);
        // 欧拉角，单位为度。
        Math::Vec3 rotation = Math::Vec3(0.0f);
        Math::Vec3 scale = Math::Vec3(1.0f);

        void rotate(const Math::Vec3& delta) {
            rotation = Math::wrap_degrees(rotation + delta);
        }

        [[nodiscard]] Math::Mat4 to_matrix() const {
            return Math::compose_trs(translation, rotation, scale);
        }
    };

    struct COMET_API RelationshipComponent {
        EntityId parent = INVALID_ENTITY_ID;
    };

    struct COMET_API WorldTransformComponent {
        Math::Mat4 world_matrix = Math::Mat4(1.0f);
        Math::Mat4 camera_world_matrix = Math::Mat4(1.0f);
    };

    template<typename T>
    inline constexpr bool is_scene_managed_component_v =
        std::is_same_v<std::remove_cvref_t<T>, IdComponent>
        || std::is_same_v<std::remove_cvref_t<T>, UuidComponent>
        || std::is_same_v<std::remove_cvref_t<T>, NameComponent>
        || std::is_same_v<std::remove_cvref_t<T>, RelationshipComponent>
        || std::is_same_v<std::remove_cvref_t<T>, WorldTransformComponent>;

    template<typename T>
    inline constexpr bool is_scene_read_only_component_v =
        std::is_same_v<std::remove_cvref_t<T>, IdComponent>
        || std::is_same_v<std::remove_cvref_t<T>, UuidComponent>
        || std::is_same_v<std::remove_cvref_t<T>, RelationshipComponent>
        || std::is_same_v<std::remove_cvref_t<T>, WorldTransformComponent>;

    struct COMET_API MeshRendererComponent {
        AssetHandle mesh;
        AssetHandle material;
    };

    struct COMET_API CameraComponent {
        bool primary = false;
        // 垂直视场角，单位为度。
        float fov = 45.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
    };
}
