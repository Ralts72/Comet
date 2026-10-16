#include "../common/surface.glsl"
#include "../common/fog.glsl"
#include "../lighting/volumetric_fog.glsl"

#ifdef COMET_GBUFFER
layout(location = 0) out vec4 g_base_metallic;
layout(location = 1) out vec4 g_normal_roughness;
layout(location = 2) out vec4 g_emission_occlusion;
layout(location = 3) out vec4 g_geometry;
#else
layout(location = 0) out vec4 color;
#endif
layout(set = 1, binding = 0, std140) uniform MaterialData {
    vec4 color;
    float intensity;
} material;

void main() {
    float alpha = surface_alpha(material.color.a);
#ifdef COMET_GBUFFER
    g_base_metallic = vec4(0.0);
    g_normal_roughness = vec4(0, 0, 0, 1);
    g_emission_occlusion = vec4(clamp(material.color.rgb * material.intensity, 0.0, 65504.0), 1);
    g_geometry = vec4(0, 0, 0, 2);
#else
    color = vec4(apply_volumetric_fog_at_depth(apply_distance_fog(material.color.rgb * material.intensity), gl_FragCoord.z), alpha);
#endif
}
