#ifndef COMET_SURFACE_LIGHTING_GLSL
#define COMET_SURFACE_LIGHTING_GLSL
#include "../common/frame.glsl"
#include "../common/surface.glsl"
#include "forward.glsl"
#include "ambient_occlusion.glsl"
#include "irradiance.glsl"
#include "../environment/lighting.glsl"

vec3 evaluate_brdf(vec3 normal, vec3 view, vec3 light, vec3 base_color,
    float metallic, float roughness) {
    float n_dot_l = clamp(dot(normal, light), 0.0, 1.0);
    float n_dot_v = clamp(dot(normal, view), 0.0, 1.0);
    vec3 half_vector = view + light;
    float half_length = length(half_vector);
    if(n_dot_l <= 0.0 || n_dot_v <= 0.0 || half_length < 1e-6)
        return vec3(0.0);
    vec3 halfway = half_vector / half_length;
    float n_dot_h = clamp(dot(normal, halfway), 0.0, 1.0);
    float v_dot_h = clamp(dot(view, halfway), 0.0, 1.0);
    float alpha = roughness * roughness;
    float alpha_squared = alpha * alpha;
    // Avoid cancellation near N.H = 1 at low roughness.
    float denominator = (1.0 - n_dot_h * n_dot_h) + alpha_squared * n_dot_h * n_dot_h;
    float distribution = alpha_squared / (3.141592653589793 * denominator * denominator);
    float visibility = 0.5 / max(
        n_dot_v * sqrt(alpha_squared + (1.0 - alpha_squared) * n_dot_l * n_dot_l)
        + n_dot_l * sqrt(alpha_squared + (1.0 - alpha_squared) * n_dot_v * n_dot_v), 1e-6);
    float grazing = pow(1.0 - v_dot_h, 5.0);
    float dielectric_fresnel = 0.04 + 0.96 * grazing;
    vec3 metal_fresnel = base_color + (1.0 - base_color) * grazing;
    vec3 fresnel = mix(vec3(dielectric_fresnel), metal_fresnel, metallic);
    vec3 diffuse = (1.0 - metallic) * (1.0 - dielectric_fresnel)
        * base_color / 3.141592653589793;
    return (diffuse + distribution * visibility * fresnel) * n_dot_l;
}

vec3 evaluate_surface_lighting(vec3 position, vec3 normal, vec3 geometric_normal,
    vec3 view, vec3 base_color, float metallic, float roughness, float occlusion,
    vec3 position_dx, vec3 position_dy) {
    vec3 result = evaluate_environment(position, normal, view, base_color, metallic, roughness)
        * occlusion * ambient_visibility(position, geometric_normal);
    for(int index = 0; index < int(lighting.light_count); ++index) {
        vec3 direction, radiance;
        if(!sample_light(index, position, direction, radiance)) continue;
        result += radiance * evaluate_brdf(normal, view, direction, base_color, metallic, roughness)
            * shadow_visibility(index, position, position_dx, position_dy, max(dot(normal, direction), 0.0));
    }
    return result;
}
#endif
