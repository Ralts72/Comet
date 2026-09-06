#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "lighting.glsl"

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 0) out vec4 color;
layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 base_color;
    float metallic;
    float roughness;
} material;

vec3 evaluate_brdf(vec3 n, vec3 v, vec3 l, vec3 base_color, float metallic, float roughness) {
    float n_dot_l = clamp(dot(n, l), 0.0, 1.0);
    float n_dot_v = clamp(dot(n, v), 0.0, 1.0);
    vec3 half_vector = v + l;
    float half_length = length(half_vector);
    if(n_dot_l <= 0.0 || n_dot_v <= 0.0 || half_length < 1e-6)
        return vec3(0.0);
    vec3 h = half_vector / half_length;
    float n_dot_h = clamp(dot(n, h), 0.0, 1.0);
    float v_dot_h = clamp(dot(v, h), 0.0, 1.0);
    float alpha = roughness * roughness;
    float alpha_squared = alpha * alpha;
    // 此写法避免低粗糙度、N·H≈1 时的消减误差。
    float denominator = (1.0 - n_dot_h * n_dot_h) + alpha_squared * n_dot_h * n_dot_h;
    float distribution = alpha_squared / (3.141592653589793 * denominator * denominator);
    float visibility = 0.5 / max(n_dot_v * sqrt(alpha_squared + (1.0 - alpha_squared) * n_dot_l * n_dot_l)
        + n_dot_l * sqrt(alpha_squared + (1.0 - alpha_squared) * n_dot_v * n_dot_v), 1e-6);
    float grazing = pow(1.0 - v_dot_h, 5.0);
    float dielectric_fresnel = 0.04 + 0.96 * grazing;
    vec3 metal_fresnel = base_color + (1.0 - base_color) * grazing;
    vec3 fresnel = mix(vec3(dielectric_fresnel), metal_fresnel, metallic);
    vec3 diffuse = (1.0 - metallic) * (1.0 - dielectric_fresnel) * base_color / 3.141592653589793;
    return (diffuse + distribution * visibility * fresnel) * n_dot_l;
}

void main() {
    vec3 view_vector = frame.camera_position.xyz - world_position;
    if(frame.view_direction.w > 0.5)
        view_vector = frame.view_direction.xyz;
    float normal_length = length(world_normal);
    float view_length = length(view_vector);
    vec3 result = vec3(0.0);
    if(normal_length > 1e-6 && view_length > 1e-6 && !isinf(normal_length) && !isinf(view_length)) {
        vec3 n = world_normal / normal_length;
        vec3 v = view_vector / view_length;
        vec3 base_color = clamp(material.base_color.rgb, 0.0, 1.0);
        float metallic = clamp(material.metallic, 0.0, 1.0);
        float roughness = clamp(material.roughness, 0.045, 1.0);
        for(int index = 0; index < int(lighting.counts.x); ++index) {
            vec3 l, radiance;
            if(!sample_light(index, world_position, l, radiance))
                continue;
            result += radiance * evaluate_brdf(n, v, l, base_color, metallic, roughness)
                * shadow_visibility(index, world_position, n, max(dot(n, l), 0.0));
        }
    }
    color = vec4(clamp(result, 0.0, 65504.0), 1.0);
}
