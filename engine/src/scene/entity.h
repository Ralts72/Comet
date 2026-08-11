#pragma once
#include "common/export.h"
#include "scene/components.h"
#include <entt.hpp>

namespace Comet {
    class Scene;
    class SceneSerializer;

    class COMET_API Entity {
    public:
        Entity() = default;

        [[nodiscard]] EntityId get_id() const;

        [[nodiscard]] EntityUuid get_uuid() const;

        template<typename T, typename... Args>
        T& add_component(Args&&... args);

        template<typename T>
        T& get_component();

        template<typename T>
        [[nodiscard]] bool has_component() const;

        template<typename T>
        const T& get_component() const;

        template<typename T>
        void remove_component() const;

        [[nodiscard]] explicit operator bool() const;

        [[nodiscard]] bool operator==(const Entity& other) const {
            return m_handle == other.m_handle && m_scene == other.m_scene;
        }

        [[nodiscard]] bool operator!=(const Entity& other) const {
            return !(*this == other);
        }

    private:
        friend class Scene;
        friend class SceneSerializer;

        Entity(entt::entity handle, Scene* scene);

        entt::entity m_handle = entt::null;
        Scene* m_scene = nullptr;
    };
}
