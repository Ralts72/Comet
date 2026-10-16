#version 450

layout(location = 0) in vec4 position_size;
layout(location = 1) in vec4 color;
layout(location = 2) in vec4 uv_rect;
layout(location = 0) out vec2 coordinate;
layout(location = 1) out vec4 tint;
layout(location = 2) flat out vec4 texture_region;
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
    texture_region = uv_rect;
    vec3 offset = (camera.right.xyz * coordinate.x + camera.up.xyz * coordinate.y)
        * position_size.w * 0.5;
    gl_Position = camera.view_projection * vec4(position_size.xyz + offset, 1);
}
