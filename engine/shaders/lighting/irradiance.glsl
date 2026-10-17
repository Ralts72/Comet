#include "irradiance_texture.glsl"
#include "irradiance_sampling.glsl"
#include "irradiance_blending.glsl"
bool sample_irradiance(vec3 position, vec3 normal, out vec3 result) {
    return sample_irradiance_regions(lighting.irradiance_volumes, int(lighting.irradiance_metadata.x),
        position, normal, 0.0, result);
}
