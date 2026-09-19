struct Light {
    vec4 position_type;
    vec4 direction_range;
    vec4 color_intensity;
    vec4 cone;
};
layout(set = 0, binding = 1, std140) uniform LightingData {
    Light lights[32];
    vec4 counts;
} lighting;

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
        result += light.color_intensity.rgb * light.color_intensity.w * attenuation
            * max(dot(n, l), 0.0) / 3.141592653589793;
    }
    return result;
}
