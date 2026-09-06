#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec3 normal;
layout(location = 0) out vec2 uv;

layout(set = 0, binding = 0, std140) uniform FrameData {
    mat4 view;
    mat4 projection;
} frame;

layout(push_constant) uniform ObjectData {
    mat4 model;
} object;

void main() {
    gl_Position = frame.projection * frame.view * object.model * vec4(position, 1.0);
    uv = texcoord;
}
