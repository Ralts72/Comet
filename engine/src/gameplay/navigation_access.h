#pragma once

#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"

namespace Comet {
    class Scene;
    class Entity;
    class PhysicsAccess;

    class COMET_API NavigationAccess {
    public:
        enum class Status { Moving, Arrived, NoPath, Blocked, BudgetExceeded, Disabled };
        virtual ~NavigationAccess() = default;
        [[nodiscard]] virtual Result<Status> move_to(
            Scene&, Entity, PhysicsAccess&, Math::Vec3 target, bool run = false) = 0;
        [[nodiscard]] virtual bool is_traversing_link(const Scene&, Entity) const = 0;
        [[nodiscard]] static const char* name(Status);
    };
}
