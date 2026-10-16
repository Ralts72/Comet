#ifndef COMET_FRAME_GLSL
#define COMET_FRAME_GLSL

layout(set = 0, binding = 0, std140) uniform FrameData {
    mat4 view;
    mat4 projection;
    vec3 camera_position;
    float orthographic;
    vec3 view_direction;
    float fog_start;
    vec4 fog_color; // RGB linear color, W inverse distance range; zero disables fog.
} frame;

// Axial distance for the engine's perspective and orthographic projections.
float view_distance_at_depth(float depth) {
    return (depth * frame.projection[3][3] - frame.projection[3][2])
        / (depth * frame.projection[2][3] - frame.projection[2][2]);
}

#endif
