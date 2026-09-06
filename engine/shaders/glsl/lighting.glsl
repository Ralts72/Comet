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

float shadow_visibility(int light_index, vec3 position, vec3 normal, float n_dot_l) {
    if(light_index != int(lighting.shadow_parameters.x))
        return 1.0;
    vec3 projected = (lighting.shadow_view_projection * vec4(position, 1.0)).xyz;
    vec2 uv = projected.xy * 0.5 + 0.5;
    if(projected.z <= 0.0 || projected.z >= 1.0
        || any(lessThan(uv, vec2(0))) || any(greaterThan(uv, vec2(1))))
        return 1.0;
    float bias = lighting.shadow_parameters.y * (1.0 + 4.0 * (1.0 - n_dot_l));
    // 3×3 nearest PCF 的最远 texel 中心距采样位置为 1.5 texel。
    // 将接收面斜率换算到阴影 UV/depth 空间，避免把同一斜面误判为遮挡。
    vec3 axis = abs(normal.y) < 0.95 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent = cross(normal, axis);
    mat3 projection = mat3(lighting.shadow_view_projection);
    vec3 plane = cross(projection * tangent, projection * cross(normal, tangent));
    if(abs(plane.z) > 1e-8) {
        vec2 depth_gradient = -2.0 * plane.xy / plane.z;
        bias += min(1.5 * lighting.shadow_parameters.z
            * (abs(depth_gradient.x) + abs(depth_gradient.y)), 0.01);
    }
    float visibility = 0.0;
    for(int y = -1; y <= 1; ++y)
        for(int x = -1; x <= 1; ++x) {
            vec2 sample_uv = uv + vec2(x, y) * lighting.shadow_parameters.z;
            if(any(lessThan(sample_uv, vec2(0))) || any(greaterThan(sample_uv, vec2(1))))
                visibility += 1.0;
            else
                visibility += projected.z - bias <= textureLod(shadow_map, sample_uv, 0.0).r ? 1.0 : 0.0;
        }
    return visibility / 9.0;
}

bool sample_light(int index, vec3 position, out vec3 direction, out vec3 radiance) {
    Light light = lighting.lights[index];
    int type = int(light.position_type.w);
    direction = -light.direction_range.xyz;
    float attenuation = 1.0;
    if(type != 0) {
        vec3 delta = light.position_type.xyz - position;
        float distance_squared = dot(delta, delta);
        float range = light.direction_range.w;
        if(distance_squared < 1e-8 || distance_squared >= range * range || isnan(distance_squared))
            return false;
        float distance = sqrt(distance_squared);
        direction = delta / distance;
        float falloff = max(1.0 - pow(distance / range, 4.0), 0.0);
        attenuation = falloff * falloff / max(distance_squared, 0.01);
        if(type == 2) {
            float alignment = dot(-direction, light.direction_range.xyz);
            float cone_weight = step(light.cone.y, alignment);
            // 极窄锥角在 float 中可能拥有相同 cos 值，退化为硬边而不是除以零。
            if(light.cone.x - light.cone.y > 1e-6)
                cone_weight = smoothstep(light.cone.y, light.cone.x, alignment);
            attenuation *= cone_weight;
        }
    }
    radiance = light.color_intensity.rgb * light.color_intensity.w * attenuation;
    return true;
}

vec3 diffuse_lighting(vec3 position, vec3 normal) {
    float normal_length = length(normal);
    if(normal_length < 1e-6 || isnan(normal_length) || isinf(normal_length))
        return vec3(0.0);
    vec3 n = normal / normal_length;
    vec3 result = vec3(0.0);
    for(int index = 0; index < int(lighting.counts.x); ++index) {
        vec3 l, radiance;
        if(!sample_light(index, position, l, radiance))
            continue;
        float n_dot_l = max(dot(n, l), 0.0);
        result += radiance
            * n_dot_l * shadow_visibility(index, position, n, n_dot_l) / 3.141592653589793;
    }
    return result;
}
