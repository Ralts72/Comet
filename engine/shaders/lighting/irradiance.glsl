
#include "irradiance_texture.glsl"

#include "irradiance_sampling.glsl"
bool sample_irradiance_region(int region, texture2D grid_texture, vec3 position, vec3 normal,
    out vec3 result, out float spatial_weight) {
    return sample_irradiance_grid(grid_texture, lighting.irradiance_volumes[region], position, normal, result, spatial_weight);
}

bool sample_irradiance(vec3 position, vec3 normal, out vec3 result) {
    result = vec3(0.0);
    float total = 0.0;
    for(int region = 0; region < int(lighting.irradiance_metadata.x); ++region) {
        vec3 value;
        float weight;
        bool valid = false;
        // 在区域外选择纹理，避免每个球谐／可见性 texel 都重复分支。
        switch(region) {
        case 0:
            valid = sample_irradiance_region(region, irradiance_grids[0],
                position, normal, value, weight);
            break;
        case 1:
            valid = sample_irradiance_region(region, irradiance_grids[1],
                position, normal, value, weight);
            break;
        case 2:
            valid = sample_irradiance_region(region, irradiance_grids[2],
                position, normal, value, weight);
            break;
        case 3:
            valid = sample_irradiance_region(region, irradiance_grids[3],
                position, normal, value, weight);
            break;
        }
        if(valid) {
            result += value * weight;
            total += weight;
        }
    }
    // 归一化交叠区域，避免两套探针重复注入能量。
    if(total > 0.0) result /= total;
    return total > 0.0;
}
