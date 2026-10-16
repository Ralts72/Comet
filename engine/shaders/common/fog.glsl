#ifndef COMET_FOG_GLSL
#define COMET_FOG_GLSL

#include "frame.glsl"

vec3 apply_distance_fog_at_depth(vec3 radiance, float depth) {
    if(frame.fog_color.w == 0.0)
        return radiance;
    float distance = view_distance_at_depth(depth);
    float amount = clamp((distance - frame.fog_start) * frame.fog_color.w, 0.0, 1.0);
    return mix(radiance, frame.fog_color.rgb, amount);
}

vec3 apply_distance_fog(vec3 radiance) {
    return apply_distance_fog_at_depth(radiance, gl_FragCoord.z);
}

#endif
