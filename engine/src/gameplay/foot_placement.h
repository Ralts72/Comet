#pragma once
#include "scene/systems/system.h"
#include "scene/entity_id.h"
#include <array>
#include <memory>
#include <unordered_map>

namespace Comet {
    struct AnimationData;
    struct FootPlacementComponent;
    class Entity;
}
namespace Comet::Gameplay {
    // 物理完成后查询落脚点；只发布动画约束，不回写角色碰撞体或世界位置。
    class COMET_API FootPlacementSystem final: public System {
    public:
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> post_fixed_update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

    private:
        struct Foot {
            float offset = 0;
            float weight = 0;
            Math::Vec3 normal{0, 1, 0};
        };
        struct State {
            std::shared_ptr<const AnimationData> source;
            std::array<Foot, 2> feet;
            float pelvis_offset = 0;
        };
        Result<void, Error> place(
            Scene&, Entity, const FootPlacementComponent&, const Context&, State&);
        std::unordered_map<EntityId, State> m_states;
    };
}
