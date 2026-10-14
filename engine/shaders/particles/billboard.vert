#version 450

layout(location = 0) in vec4 position_size;
layout(location = 1) in vec4 color;
layout(location = 0) out vec2 coordinate;
layout(location = 1) out vec4 tint;
layout(push_constant) uniform Camera {
    mat4 view_projection;
    vec4 right;
    vec4 up;
} camera;

const vec2 corners[6] = vec2[](
    vec2(-1, -1), vec2(1, -1), vec2(1, 1),
    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    coordinate = corners[gl_VertexIndex];
    tint = color;
    vec3 offset = (camera.right.xyz * coordinate.x + camera.up.xyz * coordinate.y)
        * position_size.w * 0.5;
    gl_Position = camera.view_projection * vec4(position_size.xyz + offset, 1);
}
