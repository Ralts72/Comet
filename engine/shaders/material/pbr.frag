#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/frame.glsl"
#include "../common/surface.glsl"
#include "../lighting/forward.glsl"
#include "../environment/lighting.glsl"

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 world_tangent;
layout(location = 4) in vec3 world_bitangent;
layout(location = 0) out vec4 color;
layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 base_color;
    float metallic;
    float roughness;
    float normal_scale;
    float occlusion_strength;
    vec4 emissive_color;
    float emissive_strength;
} material;
layout(set = 1, binding = 1) uniform sampler2D base_color_texture;
layout(set = 1, binding = 2) uniform sampler2D normal_texture;
layout(set = 1, binding = 3) uniform sampler2D metallic_roughness_texture;
layout(set = 1, binding = 4) uniform sampler2D occlusion_texture;
layout(set = 1, binding = 5) uniform sampler2D emissive_texture;

vec3 mapped_normal(vec3 normal, vec3 sampled, vec3 position_dx, vec3 position_dy,
    vec2 uv_dx, vec2 uv_dy) {
    if(material.normal_scale == 0.0)
        return normal;
    vec3 tangent = world_tangent.xyz;
    vec3 bitangent = world_bitangent;
    vec3 basis_normal = world_normal;
    // 烘焙切线基整体插值；片元重新正交化会改变 MikkTSpace 法线贴图的结果。
    if(world_tangent.w == 0.0) {
        float determinant = uv_dx.x * uv_dy.y - uv_dx.y * uv_dy.x;
        if(abs(determinant) < 1e-8)
            return normal;
        tangent = (position_dx * uv_dy.y - position_dy * uv_dx.y) / determinant;
        bitangent = (position_dy * uv_dx.x - position_dx * uv_dy.x) / determinant;
        tangent -= normal * dot(normal, tangent);
        float tangent_length = length(tangent);
        if(tangent_length < 1e-8)
            return normal;
        tangent /= tangent_length;
        float handedness = sign(dot(cross(normal, tangent), bitangent));
        bitangent = cross(normal, tangent) * handedness;
        basis_normal = normal;
    }
    vec3 local = sampled * 2.0 - 1.0;
    local.xy *= material.normal_scale;
    vec3 result = mat3(tangent, bitangent, basis_normal) * local;
    float normal_length = length(result);
    if(normal_length < 1e-8)
        return normal;
    return result / normal_length;
}

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

void main() {
    vec4 base_sample = material.base_color * texture(base_color_texture, uv);
    vec3 base_color = clamp(base_sample.rgb, 0.0, 1.0);
    vec3 normal_sample = texture(normal_texture, uv).rgb;
    vec2 metal_roughness = texture(metallic_roughness_texture, uv).bg;
    float occlusion = mix(1.0, texture(occlusion_texture, uv).r, material.occlusion_strength);
    vec3 emission = material.emissive_color.rgb * material.emissive_strength * texture(emissive_texture, uv).rgb;
    // 导数在一致控制流中求值；镜像 UV 和非均匀缩放由世界空间切线基处理。
    vec3 position_dx = dFdx(world_position), position_dy = dFdy(world_position);
    vec2 uv_dx = dFdx(uv), uv_dy = dFdy(uv);
    vec3 view_vector = frame.camera_position - world_position;
    if(frame.orthographic > 0.5)
        view_vector = frame.view_direction;
    float normal_length = length(world_normal);
    float view_length = length(view_vector);
    vec3 result = vec3(0.0);
    if(normal_length > 1e-6 && view_length > 1e-6
        && !isinf(normal_length) && !isinf(view_length)) {
        vec3 normal = mapped_normal(world_normal / normal_length, normal_sample,
            position_dx, position_dy, uv_dx, uv_dy);
        if(comet_double_sided && !gl_FrontFacing)
            normal = -normal;
        vec3 view = view_vector / view_length;
        float metallic = clamp(material.metallic * metal_roughness.x, 0.0, 1.0);
        float roughness = clamp(material.roughness * metal_roughness.y, 0.045, 1.0);
        result = evaluate_environment(normal, view, base_color, metallic, roughness) * occlusion;
        for(int index = 0; index < int(lighting.light_count); ++index) {
            vec3 direction, radiance;
            if(!sample_light(index, world_position, direction, radiance))
                continue;
            result += radiance * evaluate_brdf(normal, view, direction, base_color, metallic, roughness)
                * shadow_visibility(index, world_position, position_dx, position_dy,
                    max(dot(normal, direction), 0.0));
        }
    }
    // The scene target is RGBA16F; keep intense specular highlights finite.
    color = vec4(clamp(result + emission, 0.0, 65504.0), surface_alpha(base_sample.a));
}
