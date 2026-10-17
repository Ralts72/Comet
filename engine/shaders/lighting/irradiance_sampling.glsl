#include "irradiance_grid.glsl"
#include "irradiance_basis.glsl"
#ifndef COMET_IRRADIANCE_GRID
#define COMET_IRRADIANCE_GRID texture2D
#define COMET_IRRADIANCE_FETCH(grid, coordinate) texelFetch(sampler2D(grid, irradiance_sampler), coordinate, 0)
#endif
vec2 irradiance_oct_coordinate(vec3 direction) {
    direction /= abs(direction.x) + abs(direction.y) + abs(direction.z);
    vec2 coordinate = direction.xy;
    if(direction.z < 0.0) {
        vec2 signs = mix(vec2(1.0), vec2(-1.0), lessThan(coordinate, vec2(0.0)));
        coordinate = (1.0 - abs(coordinate.yx)) * signs;
    }
    return coordinate * 0.5 + 0.5;
}

vec2 irradiance_moments(COMET_IRRADIANCE_GRID grid_texture, int probe, ivec2 texel) {
    if(texel.x < 0 || texel.x >= 8) {
        texel.x = texel.x < 0 ? -texel.x - 1 : 15 - texel.x;
        texel.y = 7 - texel.y;
    }
    if(texel.y < 0 || texel.y >= 8) {
        texel.y = texel.y < 0 ? -texel.y - 1 : 15 - texel.y;
        texel.x = 7 - texel.x;
    }
    int index = texel.y * 8 + texel.x;
    vec4 packed = COMET_IRRADIANCE_FETCH(grid_texture, ivec2(9 + index / 2, probe));
    return (index & 1) == 0 ? packed.xy : packed.zw;
}

float irradiance_visibility(COMET_IRRADIANCE_GRID grid_texture, int probe, vec3 delta, float bias) {
    float distance = length(delta);
    if(distance < 1e-6) return 1.0;
    vec2 coordinate = irradiance_oct_coordinate(delta) * 8.0 - 0.5;
    ivec2 texel = ivec2(floor(coordinate));
    vec2 fraction = fract(coordinate);
    vec2 moments = mix(
        mix(irradiance_moments(grid_texture, probe, texel), irradiance_moments(grid_texture, probe, texel + ivec2(1, 0)), fraction.x),
        mix(irradiance_moments(grid_texture, probe, texel + ivec2(0, 1)), irradiance_moments(grid_texture, probe, texel + ivec2(1)), fraction.x),
        fraction.y);
    if(distance <= moments.x + bias) return 1.0;
    float variance = max(moments.y - moments.x * moments.x, 1e-5);
    float difference = distance - moments.x;
    float probability = variance / (variance + difference * difference);
    return probability * probability * probability;
}

bool sample_irradiance_grid(COMET_IRRADIANCE_GRID grid_texture, IrradianceVolume volume, vec3 position, vec3 normal,
    out vec3 result, out float spatial_weight) {
    spatial_weight = 0.0;
    result = vec3(0.0);
    if(volume.origin_intensity.w <= 0.0) return false;
    vec3 coordinate = (position - volume.origin_intensity.xyz) / volume.spacing_bias.xyz;
    ivec3 dimensions = ivec3(volume.dimensions_blend.xyz);
    if(any(lessThan(coordinate, vec3(-0.5)))
        || any(greaterThan(coordinate, vec3(dimensions) - 0.5))) return false;
    vec3 edge = min(coordinate + 0.5, vec3(dimensions) - 0.5 - coordinate) * volume.spacing_bias.xyz;
    spatial_weight = 1.0;
    if(volume.dimensions_blend.w > 0.0) {
        vec3 weights = smoothstep(vec3(0.0), vec3(volume.dimensions_blend.w), edge);
        // 各轴相乘，使相邻区域共有的地板／顶面衰减在归一化后相消。
        // 边界上的有效黑色仍是 GI，不因零权重突然回退到 IBL。
        spatial_weight = max(weights.x * weights.y * weights.z, 1e-8);
    }
    vec3 grid = clamp(coordinate, vec3(0.0), vec3(dimensions) - 1.0);
    ivec3 lower = min(ivec3(grid), dimensions - 2);
    vec3 fraction = grid - vec3(lower);
    float bias = volume.spacing_bias.w;
    vec3 biased = position + normal * bias;
    float harmonics[9];
    irradiance_basis(normal, harmonics);
    float total = 0.0;
    bool has_probe = false;
    for(int corner = 0; corner < 8; ++corner) {
        ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, corner >> 2);
        ivec3 cell = lower + offset;
        int probe = (cell.z * dimensions.y + cell.y) * dimensions.x + cell.x;
        vec3 factors = mix(vec3(1.0) - fraction, fraction, vec3(offset));
        float weight = factors.x * factors.y * factors.z;
        if(weight == 0.0) continue;
        vec4 first = COMET_IRRADIANCE_FETCH(grid_texture, ivec2(0, probe));
        if(first.w < 0.5) continue;
        has_probe = true;
        vec3 delta = biased - (volume.origin_intensity.xyz + volume.spacing_bias.xyz * vec3(cell));
        float distance = length(delta);
        float facing = distance > 1e-6 ? 0.1 + 0.9 * max(0.0, dot(-delta / distance, normal)) : 1.0;
        weight *= facing * facing * irradiance_visibility(grid_texture, probe, delta, bias);
        vec3 irradiance = first.rgb * harmonics[0];
        for(int coefficient = 1; coefficient < 9; ++coefficient)
            irradiance += COMET_IRRADIANCE_FETCH(grid_texture, ivec2(coefficient, probe)).rgb * harmonics[coefficient];
        result += max(irradiance, vec3(0.0)) * weight;
        total += weight;
    }
    // 有有效探针但权重被遮挡时返回黑色，不回退成穿墙的全局环境光。
    if(total >= 1e-5) result /= total;
    else result = vec3(0.0);
    result *= volume.origin_intensity.w;
    return has_probe;
}
