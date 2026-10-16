#pragma once
#include "scene/systems/system.h"
#include <optional>

namespace Comet::Gameplay {
    class COMET_API CharacterAnimationSystem final: public System {
    public:
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> update(Scene&, const Context&) override;
        Result<void, Error> fixed_update(Scene&, const Context&) override;

    private:
        static void update_parameters(
            Scene&, const RuntimeServices&, std::optional<bool> fixed = std::nullopt);
    };
}
