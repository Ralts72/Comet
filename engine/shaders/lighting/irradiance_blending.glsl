bool sample_irradiance_region(IrradianceVolume volume, texture2D grid_texture, vec3 position, vec3 normal,
    float minimum_bias, out vec3 result, out float spatial_weight) {
    volume.spacing_bias.w = max(volume.spacing_bias.w, minimum_bias);
    return sample_irradiance_grid(grid_texture, volume, position, normal, result, spatial_weight);
}

bool sample_irradiance_regions(IrradianceVolume volumes[MAX_IRRADIANCE_VOLUMES], int count,
    vec3 position, vec3 normal, float minimum_bias, out vec3 result) {
    result = vec3(0.0);
    float total = 0.0;
    for(int region = 0; region < count; ++region) {
        vec3 value;
        float weight;
        bool valid = false;
        // 在区域外选择纹理，避免每个球谐／可见性 texel 都重复分支。
        switch(region) {
        case 0:
            valid = sample_irradiance_region(volumes[region], irradiance_grids[0],
                position, normal, minimum_bias, value, weight);
            break;
        case 1:
            valid = sample_irradiance_region(volumes[region], irradiance_grids[1],
                position, normal, minimum_bias, value, weight);
            break;
        case 2:
            valid = sample_irradiance_region(volumes[region], irradiance_grids[2],
                position, normal, minimum_bias, value, weight);
            break;
        case 3:
            valid = sample_irradiance_region(volumes[region], irradiance_grids[3],
                position, normal, minimum_bias, value, weight);
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
