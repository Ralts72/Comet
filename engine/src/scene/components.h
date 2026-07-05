#pragma once
#include "core/math_utils.h"
#include <cstdint>
#include <string>

namespace Comet {
    using EntityId = std::uint64_t;

    inline constexpr EntityId INVALID_ENTITY_ID = 0;

    struct IdComponent {
        EntityId id = INVALID_ENTITY_ID;
    };

    struct NameComponent {
        std::string name = "Entity";
    };

    struct TransformComponent {
        Math::Vec3 translation = Math::Vec3(0.0f);
        Math::Vec3 rotation = Math::Vec3(0.0f);
        Math::Vec3 scale = Math::Vec3(1.0f);
    };

    struct MeshRendererComponent {
        std::string mesh;
        std::string material;
    };

    struct CameraComponent {
        bool primary = false;
        float fov = 45.0f;
        float near_clip = 0.1f;
        float far_clip = 1000.0f;
    };
}
