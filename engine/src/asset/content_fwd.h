#pragma once

#include "common/export.h"

namespace Comet {
    // 首次声明统一导出可见性，供 Registry 的跨共享库类型身份使用。
    class COMET_API GameData;
    class COMET_API Prefab;
    class COMET_API Mesh;
    class COMET_API Texture;
    class COMET_API Material;
    struct COMET_API AnimationLibrary;
    struct COMET_API NavigationData;
    struct COMET_API IrradianceData;
    struct COMET_API Irradiance;
}
