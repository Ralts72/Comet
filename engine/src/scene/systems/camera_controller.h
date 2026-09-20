#pragma once

#include "scene/systems/system.h"

namespace Comet {
    // 仅控制主相机上启用的组件；输入归属由宿主决定。
    class COMET_API CameraControllerSystem final: public System {
    public:
        Result<void, Error> update(Scene& scene, const Context& context) override;
    };
}
