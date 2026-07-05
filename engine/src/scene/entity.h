#pragma once
#include "common/export.h"
#include "scene/components.h"

namespace Comet {
    class Scene;

    class COMET_API Entity {
    public:
        Entity() = default;

        [[nodiscard]] bool is_valid() const;

        [[nodiscard]] EntityId get_id() const;

        [[nodiscard]] const std::string& get_name() const;

        void set_name(std::string name);

        template<typename T, typename... Args>
        T& add_component(Args&&... args);

        template<typename T>
        [[nodiscard]] bool has_component() const;

        template<typename T>
        T& get_component() const;

        template<typename T>
        void remove_component();

        [[nodiscard]] Scene* get_scene() const { return m_scene; }

        [[nodiscard]] entt::entity get_handle() const { return m_handle; }

        [[nodiscard]] explicit operator bool() const { return is_valid(); }

        [[nodiscard]] bool operator==(const Entity& other) const;

        [[nodiscard]] bool operator!=(const Entity& other) const;

    private:
        friend class Scene;

        Entity(entt::entity handle, Scene* scene)
            : m_handle(handle), m_scene(scene) {}

        entt::entity m_handle = entt::null;
        Scene* m_scene = nullptr;
    };
}
