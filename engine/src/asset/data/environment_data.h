#pragma once

#include "asset/data/texture_data.h"

namespace Comet {
    struct EnvironmentData {
        static constexpr int IRRADIANCE_SIZE = 16;
        static constexpr int SPECULAR_SIZE = 128;
        static constexpr int BRDF_SIZE = 128;
        TextureData background;
        // Cosine convolution divided by pi; all textures contain linear radiance.
        TextureData irradiance;
        TextureData specular;
        TextureData brdf;
    };
}
