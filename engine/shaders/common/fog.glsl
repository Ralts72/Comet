#ifndef COMET_FOG_GLSL
#define COMET_FOG_GLSL

#include "frame.glsl"

float distance_fog_transmittance(float depth) {
    return 1.0 - clamp((view_distance_at_depth(depth) - frame.fog_start) * frame.fog_color.w, 0.0, 1.0);
}

vec3 apply_distance_fog_at_depth(vec3 radiance, float depth) {
    if(frame.fog_color.w == 0.0)
        return radiance;
    return mix(frame.fog_color.rgb, radiance, distance_fog_transmittance(depth));
}

vec3 apply_distance_fog(vec3 radiance) {
    return apply_distance_fog_at_depth(radiance, gl_FragCoord.z);
}

#endif
