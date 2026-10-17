#pragma once
#include "scene/systems/system.h"
#include "scene/entity_id.h"
#include <unordered_map>

namespace Comet::Gameplay {
    // 仅装饰物的根部摆动；真实角色／刚体不由风系统驱动。
    class COMET_API WindSwaySystem final: public System {
    public:
        Result<void, Error> update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct State {
            Math::Vec3 base{0}, output{0};
        };
        std::unordered_map<EntityId, State> m_states;
        double m_time = 0;
    };
}
