layout(set = 0, binding = 11) uniform sampler2D irradiance_grid;

vec2 irradiance_oct_coordinate(vec3 direction) {
    direction /= abs(direction.x) + abs(direction.y) + abs(direction.z);
    vec2 coordinate = direction.xy;
    if(direction.z < 0.0) {
        vec2 signs = mix(vec2(1.0), vec2(-1.0), lessThan(coordinate, vec2(0.0)));
        coordinate = (1.0 - abs(coordinate.yx)) * signs;
    }
    return coordinate * 0.5 + 0.5;
}

vec2 irradiance_moments(int probe, ivec2 texel) {
    if(texel.x < 0 || texel.x >= 8) {
        texel.x = texel.x < 0 ? -texel.x - 1 : 15 - texel.x;
        texel.y = 7 - texel.y;
    }
    if(texel.y < 0 || texel.y >= 8) {
        texel.y = texel.y < 0 ? -texel.y - 1 : 15 - texel.y;
        texel.x = 7 - texel.x;
    }
    int index = texel.y * 8 + texel.x;
    vec4 packed = texelFetch(irradiance_grid, ivec2(9 + index / 2, probe), 0);
    return (index & 1) == 0 ? packed.xy : packed.zw;
}

float irradiance_visibility(int probe, vec3 delta, float bias) {
    float distance = length(delta);
    if(distance < 1e-6) return 1.0;
    vec2 coordinate = irradiance_oct_coordinate(delta) * 8.0 - 0.5;
    ivec2 texel = ivec2(floor(coordinate));
    vec2 fraction = fract(coordinate);
    vec2 moments = mix(
        mix(irradiance_moments(probe, texel), irradiance_moments(probe, texel + ivec2(1, 0)), fraction.x),
        mix(irradiance_moments(probe, texel + ivec2(0, 1)), irradiance_moments(probe, texel + ivec2(1)), fraction.x),
        fraction.y);
    if(distance <= moments.x + bias) return 1.0;
    float variance = max(moments.y - moments.x * moments.x, 1e-5);
    float difference = distance - moments.x;
    float probability = variance / (variance + difference * difference);
    return probability * probability * probability;
}

bool sample_irradiance(vec3 position, vec3 normal, out vec3 result) {
    result = vec3(0.0);
    if(lighting.irradiance_origin.w <= 0.0) return false;
    vec3 coordinate = (position - lighting.irradiance_origin.xyz) / lighting.irradiance_spacing_bias.xyz;
    ivec3 dimensions = ivec3(lighting.irradiance_dimensions.xyz);
    if(any(lessThan(coordinate, vec3(-0.5)))
        || any(greaterThan(coordinate, vec3(dimensions) - 0.5))) return false;
    vec3 grid = clamp(coordinate, vec3(0.0), vec3(dimensions) - 1.0);
    ivec3 lower = min(ivec3(grid), dimensions - 2);
    vec3 fraction = grid - vec3(lower);
    float bias = lighting.irradiance_spacing_bias.w;
    vec3 biased = position + normal * bias;
    float harmonics[9] = float[9](0.2820947918,
        0.4886025119 * normal.y, 0.4886025119 * normal.z, 0.4886025119 * normal.x,
        1.0925484306 * normal.x * normal.y, 1.0925484306 * normal.y * normal.z,
        0.3153915653 * (3.0 * normal.z * normal.z - 1.0),
        1.0925484306 * normal.x * normal.z, 0.5462742153 * (normal.x * normal.x - normal.y * normal.y));
    float total = 0.0;
    bool has_probe = false;
    for(int corner = 0; corner < 8; ++corner) {
        ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, corner >> 2);
        ivec3 cell = lower + offset;
        int probe = (cell.z * dimensions.y + cell.y) * dimensions.x + cell.x;
        vec3 factors = mix(vec3(1.0) - fraction, fraction, vec3(offset));
        float weight = factors.x * factors.y * factors.z;
        if(weight == 0.0) continue;
        vec4 first = texelFetch(irradiance_grid, ivec2(0, probe), 0);
        if(first.w < 0.5) continue;
        has_probe = true;
        vec3 delta = biased - (lighting.irradiance_origin.xyz + lighting.irradiance_spacing_bias.xyz * vec3(cell));
        float distance = length(delta);
        float facing = distance > 1e-6 ? 0.1 + 0.9 * max(0.0, dot(-delta / distance, normal)) : 1.0;
        weight *= facing * facing * irradiance_visibility(probe, delta, bias);
        vec3 irradiance = first.rgb * harmonics[0];
        for(int coefficient = 1; coefficient < 9; ++coefficient)
            irradiance += texelFetch(irradiance_grid, ivec2(coefficient, probe), 0).rgb * harmonics[coefficient];
        result += max(irradiance, vec3(0.0)) * weight;
        total += weight;
    }
    // 有有效探针但权重被遮挡时返回黑色，不回退成穿墙的全局环境光。
    if(total >= 1e-5) result /= total;
    else result = vec3(0.0);
    result *= lighting.irradiance_origin.w;
    return has_probe;
}
