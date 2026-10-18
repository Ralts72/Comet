#pragma once
#include "scene/systems/system.h"
#include "scene/entity_id.h"
#include <unordered_map>

namespace Comet::Gameplay {
    // 装饰物的根部摆动或柔性顶点弯曲；不驱动角色／刚体。
    class COMET_API WindSwaySystem final: public System {
    public:
        Result<void, Error> update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct State {
            Math::Vec3 base{0}, output{0};
        };
        std::unordered_map<EntityId, State> m_states;
        std::unordered_map<EntityId, Math::Vec2> m_interactions;
        double m_time = 0;
    };
}
