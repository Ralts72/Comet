#pragma once

#include "scene/systems/system.h"
#include <memory>

namespace Comet {
    // 仅运行时持有物理世界；Scene 中的组件始终是可序列化的配置。
    class COMET_API PhysicsSystem final: public System {
    public:
        PhysicsSystem();
        ~PhysicsSystem() override;

        Result<void, Error> on_start(Scene& scene) override;
        Result<void, Error> fixed_update(Scene& scene, const Context& context) override;
        void on_stop(Scene& scene) noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
