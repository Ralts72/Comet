#pragma once

#include "asset/data/texture_data.h"

namespace Comet {
    struct EnvironmentData {
        static constexpr int IRRADIANCE_SIZE = 16;
        static constexpr int SPECULAR_SIZE = 128;
        static constexpr int BRDF_SIZE = 128;
        TextureData background;
        // 漫反射卷积已除以 π，采样时不再重复除以 π。
        TextureData irradiance;
        TextureData specular;
        TextureData brdf;
    };
}
