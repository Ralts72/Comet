struct Light {
    vec4 position_type;
    vec4 direction_range;
    vec4 color_intensity;
    vec4 cone;
};
layout(set = 0, binding = 1, std140) uniform LightingData {
    Light lights[32];
    vec4 counts;
    mat4 shadow_view_projection;
    vec4 shadow_parameters;
} lighting;
layout(set = 0, binding = 2) uniform sampler2D shadow_map;

float shadow_visibility(int index, vec3 position, float n_dot_l) {
    if(index != int(lighting.shadow_parameters.x))
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
    float bias = lighting.shadow_parameters.y * (1.0 + 4.0 * (1.0 - n_dot_l));
    // Compensate nearest-texel quantization on the receiver plane.
    bias += 0.5 * lighting.shadow_parameters.z * dot(abs(depth_gradient), vec2(1.0));
    float visible = 0.0;
    for(int y = -1; y <= 1; ++y) {
        for(int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(x, y) * lighting.shadow_parameters.z;
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

vec3 diffuse_lighting(vec3 position, vec3 normal) {
    float normal_length = length(normal);
    if(normal_length < 1e-6 || isnan(normal_length) || isinf(normal_length))
        return vec3(0.0);
    vec3 n = normal / normal_length;
    vec3 result = vec3(0.0);
    for(int index = 0; index < int(lighting.counts.x); ++index) {
        Light light = lighting.lights[index];
        int type = int(light.position_type.w);
        vec3 l = -light.direction_range.xyz;
        float attenuation = 1.0;
        if(type != 0) {
            vec3 delta = light.position_type.xyz - position;
            float distance_squared = dot(delta, delta);
            if(distance_squared < 1e-8)
                continue;
            float distance = sqrt(distance_squared);
            l = delta / distance;
            float falloff = max(1.0 - pow(distance / light.direction_range.w, 4.0), 0.0);
            attenuation = falloff * falloff / max(distance_squared, 0.01);
            if(type == 2) {
                float alignment = dot(-l, light.direction_range.xyz);
                float cone_weight = step(light.cone.y, alignment);
                // 极窄锥角在 float 中可能拥有相同 cos 值，退化为硬边而不是除以零。
                if(light.cone.x - light.cone.y > 1e-6)
                    cone_weight = smoothstep(light.cone.y, light.cone.x, alignment);
                attenuation *= cone_weight;
            }
        }
        float n_dot_l = max(dot(n, l), 0.0);
        result += light.color_intensity.rgb * light.color_intensity.w * attenuation
            * n_dot_l * shadow_visibility(index, position, n_dot_l) / 3.141592653589793;
    }
    return result;
}
