#pragma once
#include "scene/systems/system.h"
namespace Comet::Gameplay {
    class COMET_API PoseConstraintSystem final: public System {
    public:
        Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> post_fixed_update(Scene&, const Context&) override;

    private:
        Result<void, Error> place(Scene&);
    };
}
