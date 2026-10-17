#ifndef COMET_IRRADIANCE_TEXTURE_GLSL
#define COMET_IRRADIANCE_TEXTURE_GLSL
#ifndef COMET_IRRADIANCE_TEXTURE_SET
#define COMET_IRRADIANCE_TEXTURE_SET 0
#define COMET_IRRADIANCE_TEXTURE_BINDING 11
#define COMET_IRRADIANCE_SAMPLER_BINDING 13
#endif
const int MAX_IRRADIANCE_VOLUMES = 4;
layout(set = COMET_IRRADIANCE_TEXTURE_SET, binding = COMET_IRRADIANCE_TEXTURE_BINDING) uniform texture2D irradiance_grids[MAX_IRRADIANCE_VOLUMES];
layout(set = COMET_IRRADIANCE_TEXTURE_SET, binding = COMET_IRRADIANCE_SAMPLER_BINDING) uniform sampler irradiance_sampler;

// 固定索引避免要求 sampled-image 数组的动态索引或 descriptor indexing。
vec4 irradiance_texel(int region, ivec2 coordinate) {
    switch(region) {
    case 0: return texelFetch(sampler2D(irradiance_grids[0], irradiance_sampler), coordinate, 0);
    case 1: return texelFetch(sampler2D(irradiance_grids[1], irradiance_sampler), coordinate, 0);
    case 2: return texelFetch(sampler2D(irradiance_grids[2], irradiance_sampler), coordinate, 0);
    default: return texelFetch(sampler2D(irradiance_grids[3], irradiance_sampler), coordinate, 0);
    }
}
#endif
