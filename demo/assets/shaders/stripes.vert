#version 450

layout(set = 0, binding = 0, std140) uniform FrameData {
    mat4 view;
    mat4 projection;
    vec3 camera_position;
    float orthographic;
    vec3 view_direction;
    float reserved;
} frame;

layout(location = 0) in vec3 position;
layout(location = 0) out vec3 world_position;

layout(push_constant) uniform ObjectData {
    mat4 model;
} object;

void main() {
    vec4 world = object.model * vec4(position, 1.0);
    world_position = world.xyz;
    gl_Position = frame.projection * frame.view * world;
}
