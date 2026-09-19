#ifndef COMET_FRAME_GLSL
#define COMET_FRAME_GLSL

layout(set = 0, binding = 0, std140) uniform FrameData {
    mat4 view;
    mat4 projection;
    vec3 camera_position;
    float orthographic;
    vec3 view_direction;
    float reserved;
} frame;

#endif
