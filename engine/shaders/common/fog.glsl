#ifndef COMET_FOG_GLSL
#define COMET_FOG_GLSL

#include "frame.glsl"

vec3 apply_distance_fog(vec3 radiance) {
    if(frame.fog_color.w == 0.0)
        return radiance;
    // Recover axial view distance for the engine's perspective and orthographic projections.
    float depth = gl_FragCoord.z;
    float distance = (depth * frame.projection[3][3] - frame.projection[3][2])
        / (depth * frame.projection[2][3] - frame.projection[2][2]);
    float amount = clamp((distance - frame.fog_start) * frame.fog_color.w, 0.0, 1.0);
    return mix(radiance, frame.fog_color.rgb, amount);
}

#endif
