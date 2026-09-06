layout(set = 0, binding = 0, std140) uniform FrameData {
    mat4 view;
    mat4 projection;
    vec4 camera_position;
    vec4 view_direction;
} frame;
