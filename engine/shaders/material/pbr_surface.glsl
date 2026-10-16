#include "../common/frame.glsl"
#include "../common/surface.glsl"
#include "../common/fog.glsl"
#include "../lighting/surface_lighting.glsl"
#include "../lighting/volumetric_fog.glsl"

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 world_tangent;
layout(location = 4) in vec3 world_bitangent;
#ifdef COMET_GBUFFER
layout(location = 0) out vec4 g_base_metallic;
layout(location = 1) out vec4 g_normal_roughness;
layout(location = 2) out vec4 g_emission_occlusion;
layout(location = 3) out vec4 g_geometry;
#else
layout(location = 0) out vec4 color;
#endif
layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 base_color;
    float metallic;
    float roughness;
    float normal_scale;
    float occlusion_strength;
    vec4 emissive_color;
    float emissive_strength;
    float transmission;
    float ior;
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
    vec3 normal = vec3(0.0), geometric_normal = vec3(0.0);
    float metallic = clamp(material.metallic * metal_roughness.x, 0.0, 1.0);
    float roughness = clamp(material.roughness * metal_roughness.y, 0.045, 1.0);
    if(normal_length > 1e-6 && !isinf(normal_length)) {
        geometric_normal = world_normal / normal_length;
        normal = mapped_normal(geometric_normal, normal_sample,
            position_dx, position_dy, uv_dx, uv_dy);
        if(comet_double_sided && !gl_FrontFacing) {
            normal = -normal;
            geometric_normal = -geometric_normal;
        }
    }
    float alpha = surface_alpha(base_sample.a);
#ifdef COMET_GBUFFER
    g_base_metallic = vec4(base_color, metallic);
    g_normal_roughness = vec4(normal, roughness);
    g_emission_occlusion = vec4(clamp(emission, 0.0, 65504.0), occlusion);
    g_geometry = vec4(geometric_normal, 1.0);
#else
    vec3 result = vec3(0.0);
    if(length(normal) > 1e-6 && view_length > 1e-6 && !isinf(view_length))
        result = evaluate_surface_lighting(world_position, normal, geometric_normal,
            view_vector / view_length, base_color, metallic, roughness, occlusion,
            position_dx, position_dy);
    color = vec4(clamp(apply_volumetric_fog(apply_distance_fog(result + emission),
        world_position), 0.0, 65504.0), alpha);
#endif
}
