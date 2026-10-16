#ifndef COMET_LIGHTING_VOLUMETRIC_FOG_GLSL
#define COMET_LIGHTING_VOLUMETRIC_FOG_GLSL
#include "../common/frame.glsl"
#include "forward.glsl"
#include "../common/volumetric_fog.glsl"
layout(set = 0, binding = 12) uniform sampler2D fog_volume;
vec3 apply_volumetric_fog(vec3 radiance, vec3 position) {
    if(lighting.fog_parameters.x <= 0) return radiance;
    vec4 view = frame.view * vec4(position, 1);
    vec4 clip = frame.projection * view;
    vec2 uv = clip.xy / clip.w * vec2(.5, -.5) + .5;
    vec4 fog = sample_volumetric_fog(fog_volume, lighting.fog_parameters.xy, uv, abs(view.z));
    return fog.rgb + radiance * fog.a;
}
vec3 apply_volumetric_fog_at_depth(vec3 radiance, float depth) {
    if(lighting.fog_parameters.x <= 0) return radiance;
    vec4 fog = sample_volumetric_fog(fog_volume, lighting.fog_parameters.xy,
        gl_FragCoord.xy * lighting.fog_parameters.zw, abs(view_distance_at_depth(depth)));
    return fog.rgb + radiance * fog.a;
}
#endif
