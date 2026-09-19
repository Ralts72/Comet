const int LIGHT_DIRECTIONAL = 0;
const int LIGHT_SPOT = 2;

struct Light {
    vec3 position;
    float type;
    vec3 direction;
    float range;
    vec3 color;
    float intensity;
    float inner_cone_cos;
    float outer_cone_cos;
    float casts_shadow;
    float reserved;
};
layout(set = 0, binding = 1, std140) uniform LightingData {
    Light lights[32];
    float light_count;
    float excess_lights;
    float invalid_lights;
    float reserved;
    mat4 shadow_view_projection;
    float shadow_light_index;
    float shadow_depth_bias;
    float shadow_texel_size;
    float shadow_reserved;
    vec4 environment; // intensity, max LOD, sin(rotation), cos(rotation)
} lighting;
layout(set = 0, binding = 2) uniform sampler2D shadow_map;

float shadow_visibility(int index, vec3 position, float n_dot_l) {
    if(index != int(lighting.shadow_light_index))
        return 1.0;
    vec3 projected = (lighting.shadow_view_projection * vec4(position, 1.0)).xyz;
    vec2 uv = projected.xy * 0.5 + 0.5;
    vec2 uv_dx = dFdx(uv);
    vec2 uv_dy = dFdy(uv);
    float depth_dx = dFdx(projected.z);
    float depth_dy = dFdy(projected.z);
    float determinant = uv_dx.x * uv_dy.y - uv_dx.y * uv_dy.x;
    vec2 depth_gradient = vec2(0.0);
    if(abs(determinant) > 1e-12)
        depth_gradient = vec2(uv_dy.y * depth_dx - uv_dx.y * depth_dy,
            uv_dx.x * depth_dy - uv_dy.x * depth_dx) / determinant;
    if(projected.z < 0.0 || projected.z > 1.0
        || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return 1.0;
    float bias = lighting.shadow_depth_bias * (1.0 + 4.0 * (1.0 - n_dot_l));
    // Compensate nearest-texel quantization on the receiver plane.
    bias += 0.5 * lighting.shadow_texel_size * dot(abs(depth_gradient), vec2(1.0));
    float visible = 0.0;
    for(int y = -1; y <= 1; ++y) {
        for(int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(x, y) * lighting.shadow_texel_size;
            vec2 sample_uv = uv + offset;
            if(any(lessThan(sample_uv, vec2(0.0))) || any(greaterThan(sample_uv, vec2(1.0))))
                visible += 1.0;
            else {
                float receiver_depth = projected.z + dot(depth_gradient, offset) - bias;
                float sampled_depth = textureLod(shadow_map, sample_uv, 0.0).r;
                visible += receiver_depth <= sampled_depth ? 1.0 : 0.0;
            }
        }
    }
    return visible / 9.0;
}

bool sample_light(int index, vec3 position, out vec3 direction, out vec3 radiance) {
    Light light = lighting.lights[index];
    int type = int(light.type);
    direction = -light.direction;
    float attenuation = 1.0;
    if(type != LIGHT_DIRECTIONAL) {
        vec3 delta = light.position - position;
        float distance_squared = dot(delta, delta);
        if(distance_squared < 1e-8 || distance_squared >= light.range * light.range
            || isnan(distance_squared))
            return false;
        float distance = sqrt(distance_squared);
        direction = delta / distance;
        float falloff = max(1.0 - pow(distance / light.range, 4.0), 0.0);
        attenuation = falloff * falloff / max(distance_squared, 0.01);
        if(type == LIGHT_SPOT) {
            float alignment = dot(-direction, light.direction);
            float cone_weight = step(light.outer_cone_cos, alignment);
            // 极窄锥角在 float 中可能拥有相同 cos 值，退化为硬边而不是除以零。
            if(light.inner_cone_cos - light.outer_cone_cos > 1e-6)
                cone_weight = smoothstep(light.outer_cone_cos, light.inner_cone_cos, alignment);
            attenuation *= cone_weight;
        }
    }
    radiance = light.color * light.intensity * attenuation;
    return true;
}
