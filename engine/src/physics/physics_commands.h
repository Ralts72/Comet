#pragma once

#include "common/export.h"
#include "core/math_utils.h"
#include "scene/entity_id.h"
#include "scene/entity_uuid.h"

namespace Comet {
    class Entity;
    class Scene;
    class SceneRuntime;
    class PhysicsService;

    class COMET_API PhysicsCommands {
    public:
        virtual ~PhysicsCommands() = default;
        PhysicsCommands(const PhysicsCommands&) = delete;
        PhysicsCommands& operator=(const PhysicsCommands&) = delete;

        [[nodiscard]] bool is_bound_to(const Scene& scene) const noexcept {
            return m_scene == &scene;
        }
        [[nodiscard]] bool request_impulse(Entity entity, Math::Vec3 impulse);

    protected:
        PhysicsCommands() = default;
        [[nodiscard]] bool has_binding() const noexcept { return m_scene != nullptr; }

    private:
        friend class SceneRuntime;
        friend class PhysicsService;
        bool begin(const Scene& scene);
        void end() noexcept;
        virtual bool enqueue_impulse(EntityUuid uuid, EntityId entity, Math::Vec3 impulse) = 0;
        virtual void reset() noexcept = 0;

        const Scene* m_scene = nullptr;
    };
}
